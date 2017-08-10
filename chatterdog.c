#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
#include <vector>
#include <algorithm>

snd_pcm_t *OpenCapture(const char *name, unsigned int sampling_rate, int channels)
{
    int i;
    int err;
    snd_pcm_t *capture_handle;
    snd_pcm_hw_params_t *hw_params;

    if ((err = snd_pcm_open(&capture_handle, name, SND_PCM_STREAM_CAPTURE, 0)) < 0)
    {
        fprintf(stderr, "cannot open audio device %s (%s)\n",
                name,
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0)
    {
        fprintf(stderr, "cannot allocate hardware parameter structure (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_any(capture_handle, hw_params)) < 0)
    {
        fprintf(stderr, "cannot initialize hardware parameter structure (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_set_access(
             capture_handle,
             hw_params,
             SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
    {
        fprintf(stderr, "cannot set access type (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_set_format(
             capture_handle,
             hw_params,
             SND_PCM_FORMAT_S16_LE)) < 0)
    {
        fprintf(stderr, "cannot set sample format (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_set_rate_near(
             capture_handle,
             hw_params,
             &sampling_rate,
             0)) < 0)
    {
        fprintf(stderr, "cannot set sample rate (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_set_channels(capture_handle, hw_params, channels)) < 0)
    {
        fprintf(stderr, "cannot set channel count (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params(capture_handle, hw_params)) < 0)
    {
        fprintf(stderr, "cannot set parameters (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    snd_pcm_hw_params_free(hw_params);

    if ((err = snd_pcm_prepare(capture_handle)) < 0)
    {
        fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    snd_pcm_nonblock(capture_handle, 0);
    return capture_handle;
}

snd_pcm_t *OpenPlayback(const char *name, unsigned int sampling_rate, int channels)
{
    int pcm;
    snd_pcm_t *pcm_handle;
    snd_pcm_hw_params_t *params;
    snd_pcm_uframes_t frames;

    if ((pcm = snd_pcm_open(&pcm_handle, name,
                            SND_PCM_STREAM_PLAYBACK, 0)) < 0)
        printf("ERROR: Can't open \"%s\" PCM device. %s\n",
               name, snd_strerror(pcm));

    /* Allocate parameters object and fill it with default values*/
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm_handle, params);

    /* Set parameters */
    if ((pcm = snd_pcm_hw_params_set_access(pcm_handle, params,
                                            SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
        printf("ERROR: Can't set interleaved mode. %s\n", snd_strerror(pcm));

    if ((pcm = snd_pcm_hw_params_set_format(pcm_handle, params,
                                            SND_PCM_FORMAT_S16_LE)) < 0)
        printf("ERROR: Can't set format. %s\n", snd_strerror(pcm));

    if ((pcm = snd_pcm_hw_params_set_channels(pcm_handle, params, channels)) < 0)
        printf("ERROR: Can't set channels number. %s\n", snd_strerror(pcm));

    if ((pcm = snd_pcm_hw_params_set_rate_near(pcm_handle, params, &sampling_rate, 0)) < 0)
        printf("ERROR: Can't set rate. %s\n", snd_strerror(pcm));

    /* Write parameters */
    if ((pcm = snd_pcm_hw_params(pcm_handle, params)) < 0)
    {
        printf("ERROR: Can't set harware parameters. %s\n", snd_strerror(pcm));
    }
    snd_pcm_nonblock(pcm_handle, 0);
    return pcm_handle;
}

bool GetChunk(snd_pcm_t *h, std::vector<short> &data, int start, int len)
{
    int err;

    if ((err = snd_pcm_readi(h, &data[start], len) != len))
    {
        fprintf(stderr, "read from audio interface failed (%s)\n",
                snd_strerror(err));
        exit(1);
    }
}

bool HasSignal(std::vector<short>::iterator beg,
               std::vector<short>::iterator end,
               short threshold,
               int minLen)
{
    auto no_above_threshold = std::count_if(beg, end,
                                            [threshold](short s) { return s > threshold; });
    return no_above_threshold > minLen;
}

int GetNoise(snd_pcm_t *h, std::vector<short> &data)
{
    bool overTh = false;

    // read chunks while there is some noise;
    int chunkCount = 0;
    const int chunkSize = 15000;
    const short thresh = 4000;
    const int minLen = 1000;
    do
    {
        int start = chunkCount * chunkSize;
        GetChunk(h, data, start, chunkSize);
        if (HasSignal(data.begin() + start,
                      data.begin() + start + chunkSize, thresh,
                      minLen))
        {
            chunkCount++;
            fprintf(stderr, "s");
        }
        else
        {
            snd_pcm_prepare(h);
            break;
        }
    } while ((chunkCount + 1) * chunkSize < data.size());

    return chunkCount * chunkSize;
}

void Play(snd_pcm_t *playback, std::vector<short> &buf, int len)
{
    int err;
    if (err = snd_pcm_writei(playback, buf.data(), len) < 0)
    {
        printf("err %d\n", err);
        snd_pcm_prepare(playback);
        err = snd_pcm_writei(playback, buf.data(), len);
    }
}

int Compress(std::vector<short> &buf, int len)
{
    const float factor = 1.4;

    for (float source = factor; source < len; source += factor)
    {
        int dest = source / factor;
        buf[floor(dest + 0.5)] = buf[floor(source + 0.5)];
    }

    return len / factor;
}

int main(int argc, char *argv[])
{
    snd_pcm_t *capture = OpenCapture(argv[1], 44100, 1);
    snd_pcm_t *playback = OpenPlayback(argv[2], 44100, 1);
    const int bufsize = 500000;
    std::vector<short> buf(bufsize);
    int err;
    for (;;)
    {
        printf("\nStart\n");
        std::fill(buf.begin(), buf.end(), 0);
        int noiseLen = GetNoise(capture, buf);
        if (noiseLen > 0)
        {
            printf("noise %d\n", noiseLen);
            auto compLen = Compress(buf, noiseLen);
            Play(playback, buf, compLen);
            printf("Played\n");
        }
    }

    snd_pcm_close(capture);
    snd_pcm_close(playback);
    exit(0);
}
