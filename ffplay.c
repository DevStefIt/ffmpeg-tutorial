#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>

#include <SDL.h>
#include <stdbool.h>
#include <assert.h>

#undef main

#define SAMPLE_RATE 48000
#define CHANNELS_NUMBER 2

#define BYTES_PER_SAMPLE 2
#define NUM_OF_SAMPLES 2048

#define AUDIO_BUFFER_SIZE 65536

#define WINDOW_ORIG_X 100
#define WINDOW_ORIG_Y 100
#define WINDOW_WIDTH 720
#define WINDOW_HEIGHT 480

#define PICTURE_QUEUE_SIZE 1

typedef struct MediaContainer
{
    // 格式上下文
    AVFormatContext *format_ctx;
    // 视频流索引
    int video_stream_idx;
    // 音频流索引
    int audio_stream_idx;
    // 视频流
    AVStream *video_stream;
    // 音频流
    AVStream *audio_stream;
} MediaContainer;

typedef struct Decoder
{
    AVCodec *codec;
    AVCodecContext *codec_ctx;
} Decoder;

typedef struct AudioDevice
{
    SDL_AudioSpec audio_spec;
    SDL_AudioDeviceID id;

} AudioDevice;

typedef struct AudioResamlpe
{
    AVFrame *frame;
    struct SwrContext *swr_ctx;
} AudioResamlpe;

typedef struct PacketQueue
{
    AVPacketList *first_packet;
    AVPacketList *lasst_packet;

    int num_of_packets;
    int num_of_bytes;

    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

typedef struct VideoDevice
{
    SDL_Window *window;
    SDL_Renderer *renderer;
} VideoDevice;

typedef struct VideoRescale
{
    struct SwsContext *sws_ctx;
} VideoRescale;

typedef struct Picture
{
    SDL_Texture *texture;
    SDL_mutex *mutex;
    int width;
    int height;
    double pts;
}Picture;
typedef struct PictureQueue
{
    int size;
    int wr_index;
    int rd_index;
    SDL_mutex *mutex;
    SDL_cond *cond;
    Picture pictures[PICTURE_QUEUE_SIZE];
}PictureQueue;

// 视频宽高比
static double aspect_ratio = 0.0;

// 媒体容器
static MediaContainer media_container;

static Decoder audio_decoder;
static Decoder video_decoder;

static AudioDevice audio_device;
static AudioResamlpe audio_resample;

int8_t audio_buffer[AUDIO_BUFFER_SIZE];
static unsigned int audio_buffer_index = 0;
static unsigned int audio_data_length = 0;

static PacketQueue audio_queue;
static PacketQueue video_queue;

static double audio_clock = 0.0;
static double video_clock = 0.0;

static VideoDevice video_device;
static VideoRescale video_rescale;

// 初始化媒体容器
static bool init_media_container(MediaContainer *media_container, const char *filename)
{
    assert(media_container != NULL);
    AVFormatContext *format_ctx = NULL;
    int video_stream_idx = -1;
    int audio_stream_idx = -1;
    AVStream *video_stream = NULL;
    AVStream *audio_stream = NULL;

    // 打开输入文件
    if (avformat_open_input(&format_ctx, filename, NULL, NULL) != 0)
    {
        fprintf(stderr, "Could not open file %s\n", filename);
        return false;
    }
    // 设置媒体容器格式上下文
    media_container->format_ctx = format_ctx;

    // 查找流信息
    if (avformat_find_stream_info(format_ctx, NULL) < 0)
    {
        fprintf(stderr, "Could not find stream info\n");
        return false;
    }
    // 打印格式上下文信息
    av_dump_format(format_ctx, 0, filename, 0);

    // 查找最佳视频流
    video_stream_idx = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_idx < 0)
    {
        fprintf(stderr, "Could not find video stream in input file\n");
        return false;
    }
    // 查找最佳音频流
    audio_stream_idx = av_find_best_stream(format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_stream_idx < 0)
    {
        fprintf(stderr, "Could not find audio stream in input file\n");
        return false;
    }
    // 设置媒体容器视频流索引
    media_container->video_stream_idx = video_stream_idx;
    // 设置媒体容器音频流索引
    media_container->audio_stream_idx = audio_stream_idx;
    printf("video stream index: %d\n", video_stream_idx);
    printf("audio stream index: %d\n", audio_stream_idx);

    // 获取视频流和音频流
    video_stream = format_ctx->streams[video_stream_idx];
    audio_stream = format_ctx->streams[audio_stream_idx];

    // 设置媒体容器视频流和音频流
    media_container->video_stream = video_stream;
    media_container->audio_stream = audio_stream;
    return true;
}

// 释放媒体容器
static void release_media_container(MediaContainer *media_container)
{
    assert(media_container);
    printf("close format context.\n");
    if (media_container->format_ctx != NULL)
    {
        avformat_close_input(&media_container->format_ctx);
    }
    // 设置媒体容器格式上下文为空
    media_container->format_ctx = NULL;
    // 设置媒体容器视频流和音频流为空
    media_container->video_stream = NULL;
    media_container->audio_stream = NULL;
    // 设置媒体容器视频流索引和音频流索引为-1
    media_container->video_stream_idx = -1;
    media_container->audio_stream_idx = -1;
}

static bool init_audio_decoder(Decoder *decoder, AVStream *stream)
{
    assert(decoder != NULL);
    assert(stream != NULL);

    AVCodec *codec = NULL;
    AVCodecContext *codec_ctx = NULL;

    codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
    {
        fprintf(stderr, "Codec not found.\n");
        return false;
    }
    decoder->codec = codec;

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx)
    {
        fprintf(stderr, "Could not allocate audio codec context.\n");
        return false;
    }
    decoder->codec_ctx = codec_ctx;
    if (avcodec_parameters_to_context(codec_ctx, stream->codecpar) < 0)
    {
        fprintf(stderr, "Could not copy audio codec parameters to decoder context!\n");
        return false;
    }

    if (avcodec_open2(codec_ctx, codec, NULL) < 0)
    {
        fprintf(stderr, "Could not open audio codec!\n");
        return false;
    }
    return true;
}

static bool init_video_decoder(Decoder *decoder, AVStream *stream)
{
    assert(decoder != NULL);
    assert(stream != NULL);

    AVCodec *codec = NULL;
    AVCodecContext *codec_ctx = NULL;

    codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
    {
        fprintf(stderr, "Codec not found.\n");
        return false;
    }
    decoder->codec = codec;
    if (avcodec_parameters_to_context(codec_ctx, stream->codecpar) < 0)
    {
        fprintf(stderr, "Could not copy video codec parameters to decoder context!\n");
        return false;
    }

    if (avcodec_open2(codec_ctx, codec, NULL) < 0)
    {
        fprintf(stderr, "Could not open video codec!\n");
        return false;
    }
    return true;
}

static int decode_audio(void *userdata, uint8_t *audio_buffer, int buffer_size)
{
    AVCodecContext *codec_ctx = audio_decoder.codec_ctx;
    assert(codec_ctx != NULL);

    struct SwrContext *swr_ctx = audio_resample.swr_ctx;
    assert(swr_ctx != NULL);

    AVFrame *frame = audio_resample.frame;
    assert(frame != NULL);

    AVPacket packet;
    int ret = packet_queue_get(&audio_queue, &packet, true);
    if (ret == -1)
    {
        fprintf(stderr, "Could not get an audio packet from queue - an error! \n");
        return -1;
    }
    else if (ret == 0)
    {
        fprintf(stderr, "Could not get an audio packet frmom queue - no data! \n");
        return 0;
    }

    ret = avcodec_send_packet(codec_ctx, &packet);
    if (ret < 0)
    {
        fprintf(stderr, "Error sending packet (%s)\n", av_err2str(ret));
        av_packet_unref(&packet);
        return -1;
    }

    int length = 0;
    while (ret >= 0)
    {
        // 接收编码帧
        ret = avcodec_receive_frame(codec_ctx, frame);
        // 如果返回AVERROR(EAGAIN)或者AVERROR_EOF，则表示没有可用的帧
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0)
        {
            fprintf(stderr, "Error while receiving frame from thr decoder (%s)\n", av_err2str(ret));
            break;
        }
        // 计算延迟采样数
        int nsamples_delay = swr_get_delay(swr_ctx, frame->sample_rate);
        // 计算转换后的采样数
        int nsamples = av_rescale_rnd(nsamples_delay + frame->nb_samples, codec_ctx->sample_rate, SAMPLE_RATE, AV_ROUND_UP);

        // 分配内存空间
        uint8_t **samples = NULL;
        if (av_samples_alloc_array_and_samples(&samples, NULL, CHANNELS_NUMBER, nsamples, AV_SAMPLE_FMT_S16, 0) < 0)
        {
            fprintf(stderr, "Could not allocate memory to store audiosamples!\n");
            break;
        }

        if (av_samples_alloc(samples, NULL, CHANNELS_NUMBER, nsamples, AV_SAMPLE_FMT_S16, 0) < 0)
        {
            fprintf(stderr, "Could not allocate memory to store audiosamples!\n");
            av_freep(&samples);
            break;
        }
        // 转换采样
        int nsamples_convered = swr_convert(swr_ctx, samples, nsamples, (const uint8_t **)frame->data, frame->nb_samples);
        // 计算转换后的字节数
        int nbytes = av_samples_get_buffer_size(NULL, CHANNELS_NUMBER, nsamples_convered, AV_SAMPLE_FMT_S16, 1);
        // 判断是否溢出
        assert((length + nbytes) <= buffer_size);
        // 将转换后的采样数据拷贝到buffer中
        memcpy(audio_buffer, samples[0], nbytes);

        // 指向下一个样本的指针
        audio_buffer += nbytes;
        // 当前音频缓冲区长度
        length += nbytes;
        if (samples)
            av_freep(&samples[0]);
        av_freep(&samples);
        audio_clock += (double)nbytes / (double)(CHANNELS_NUMBER * BYTES_PER_SAMPLE * codec_ctx->sample_rate);
    }
    av_packet_unref(&packet);
    return length;
}

static void audio_callback(void *userdata, Uint8 *stream, int length)
{
    int decoded_length = 0;
    int audio_chunk_length = 0;

    SDL_memset(stream, 0, length);
    while (length > 0)
    {
        if (audio_buffer_index >= audio_data_length)
        {
            decoded_length = decode_audio(userdata, audio_buffer, sizeof(audio_buffer));
            if (decoded_length < 0)
            {
                audio_data_length = 1024;
                memset(audio_buffer, 0, audio_data_length);
            }
            else
            {
                audio_data_length = decoded_length;
            }

            audio_buffer_index = 0;
        }

        audio_chunk_length = audio_data_length - audio_buffer_index;
        if (audio_chunk_length > length)
            audio_chunk_length = length;

        const Uint8 *src = &audio_buffer[audio_buffer_index];
        SDL_MixAudioFormat(stream, src, AUDIO_S16SYS, audio_chunk_length, SDL_MIX_MAXVOLUME);

        length -= audio_chunk_length;
        stream += audio_chunk_length;
        audio_buffer_index += audio_chunk_length;
    }
}

static bool init_audio_device(AudioDevice *device)
{
    SDL_AudioSpec audio_spec;
    SDL_memset(&audio_spec, 0, sizeof(audio_spec));

    audio_spec.freq = SAMPLE_RATE;
    audio_spec.format = AUDIO_S16SYS;
    audio_spec.channels = CHANNELS_NUMBER;
    audio_spec.silence = 0;
    audio_spec.samples = NUM_OF_SAMPLES;
    audio_spec.callback = audio_callback;
    audio_spec.userdata = NULL;

    int audio_device_id = SDL_OpenAudioDevice(NULL, 0, &audio_spec, &device->audio_spec, 0);
    if (audio_device_id == 0)
    {
        fprintf(stderr, "SDL_OpenAudioDevice() error: %s\n", SDL_GetError());
        return false;
    }
    device->id = audio_device_id;
    return true;
}

static void release_audio_device(AudioDevice *device)
{
    assert(device != NULL);
    if (device->id != 0)
        SDL_CLoseAudioDevice(device->id);
    SDL_memset(&device->audio_spec, 0, sizeof(device->audio_spec));
    device->id = -1;
}

static bool init_audio_resample(AudioResamlpe *resample,
                                int in_channel_count, int out_channel_count,
                                int in_sample_format, int out_sample_format,
                                int in_channel_layout, int out_channel_layout,
                                int in_sample_rate, int out_sample_rate)
{
    assert(resample != NULL);
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Could not allocate audio frame!\n");
        return false;
    }

    struct SwrContext *swr_ctx = swr_alloc();
    if (!swr_ctx)
    {
        fprint(stderr, "Could not allocate resample context!\n");
        av_frame_free(&frame);
        return false;
    }

    av_opt_set_int(swr_ctx, "in_channel_count", in_channel_count, 0);
    av_opt_set_int(swr_ctx, "out_channel_count", in_channel_count, 0);
    av_opt_set_int(swr_ctx, "in_sample_fmt", in_sample_format, 0);
    av_opt_set_int(swr_ctx, "out_sample_fmt", out_sample_format, 0);
    av_opt_set_int(swr_ctx, "in_channel_layout", in_channel_layout, 0);
    av_opt_set_int(swr_ctx, "out_channel_layout", out_channel_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", in_sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", out_sample_rate, 0);

    swr_init(swr_ctx);

    resample->frame = frame;
    resample->swr_ctx = swr_ctx;

    return true;
}

static void release_audio_resample(AudioResamlpe *resample)
{
    assert(resample != NULL);
    if (resample->frame != NULL)
        av_frame_free(&resample->frame);
    resample->frame = NULL;
    if (resample->swr_ctx != NULL)
        swr_free(&resample->swr_ctx);
    resample->swr_ctx = NULL;
}

static bool init_video_device(VideoDevice *device, int xorig, int yorig, int width, int height)
{
    assert(device != NULL);
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;

    window = SDL_CreateWindow("player", xorig, yorig, width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
    {
        fprintf(stderr, "SDL_CreateRenderer() error:%s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        return false;
    }
    device->window = window;
    device->renderer = renderer;
    return true;
}

static void release_video_device(VideoDevice *device)
{
    assert(device != NULL);
    if (device->renderer != NULL)
        SDL_DestoryRenderer(device->renderer);
    device->renderer = NULL;

    if (device->window != NULL)
        SDL_DertoryWindow(device->window);
    device->window = NULL;
}

static bool init_video_rescale(VideoRescale *rescale,
                               int in_width, int in_height, int in_pix_format,
                               int out_width, int out_height, int out_pix_format)
{
    assert(rescale != NULL);
    struct SwsContext *sws_ctx = sws_getContext(in_width, in_height, in_pix_format,
                                                out_width, out_height, out_pix_format,
                                                SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx)
    {
        fprintf(stderr, "Could not allocate scale context!\n");
        return false;
    }
    rescale->sws_ctx = sws_ctx;
    return true;
}

static void release_video_rescale(VideoRescale *rescale)
{
    assert(rescale != NULL);
    if (rescale->sws_ctx != NULL)
        sws_freeDontext(rescale->sws_ctx);
    rescale->sws_ctx = NULL;
}

static bool packet_queue_init(PacketQueue *queue)
{
    assert(queue!=NULL);
    
    queue->first_packet =NULL;
    queue->lasst_packet=NULL;
    queue->num_of_packets = 0;
    queue->num_of_bytes = 0;

    queue->mutex = SDL_CreateMutex();
    if(!queue->mutex)
    {
        fprintf(stderr,"SDL_CreateMutex() error:%s\n",SDL_GetError());
        return false;
    }
    queue->cond = SDL_CreateCond();
    if(!queue->cond)
    {
        fprintf(stderr,"SDL_CreateCond() error:%s\n",SDL_GetError());
        return false;
    }
    return true;
}

static bool picture_queue_init(PictureQueue *queue)
{
    assert(queue!=NULL);

    queue->size = 0;
    queue->wr_index=0;
    queue->rd_index=0;

    queue->mutex=SDL_CreateMutex();
    if(!queue->cond)
    {
        fprintf(stderr,"SDL_CreateCond() error:%s\n",SDL_GetError());
        return false;
    }
    queue->cond = SDL_CreateCond();
    if (!queue->cond)
    {
        fprintf(stderr, "SDL_CreateCond() error: %s\n", SDL_GetError());
        SDL_DestroyMutex(queue->mutex);
        return false;
    }
    int i = 0;
    Picture *picture = &queue->pictures[0];
    for(;i<PICTURE_QUEUE_SIZE;i++,picture++)
    {
        picture->texture = NULL;
        picture->width = 0;
        picture->height = 0;
        picture->mutex = SDL_CreateMutex();
        if (!picture->mutex)
        {
            fprintf(stderr, "SDL_CreateMutex() error: %s\n", SDL_GetError());
            while (i-- >= 0)
            {
                picture--;
                if (picture->mutex)
                    SDL_DestroyMutex(picture->mutex);
                picture->mutex = NULL;
            }
            return false;
        }
    }
    return true;
}


int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: %s file\n", argv[0]);
        return -1;
    }
    // 初始化SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        goto end;
    }
    // 初始化媒体容器
    if (!init_media_container(&media_container, argv[1]))
    {
        fprintf(stderr, "Could not initialize media container\n");
        goto end;
    }
    // 设置视频宽高比
    if (media_container.video_stream->codecpar->sample_aspect_ratio.num != 0)
    {
        aspect_ratio = av_q2d(media_container.video_stream->codecpar->sample_aspect_ratio) *
                       media_container.video_stream->codecpar->width / media_container.video_stream->codecpar->height;
        printf("aspect_ratio = %f\n", aspect_ratio);
        printf("video_width = %d\n", media_container.video_stream->codecpar->width);
        printf("video_height = %d\n", media_container.video_stream->codecpar->height);
        printf("video_sar_num = %d\n", media_container.video_stream->codecpar->sample_aspect_ratio.num);
    }
    if (aspect_ratio <= 0)
    {
        aspect_ratio = (double)media_container.video_stream->codecpar->width / (double)media_container.video_stream->codecpar->height;
        printf("aspect_ratio = %f\n", aspect_ratio);
        printf("media width = %d\n", media_container.video_stream->codecpar->width);
        printf("media height = %d\n", media_container.video_stream->codecpar->height);
    }
    if (!init_audio_decoder(&audio_decoder, media_container.video_stream))
    {
        fprintf(stderr, "init_audio_decoder() failed!\n");
        goto end;
    }
    if (!init_video_decoder(&video_decoder, media_container.video_stream))
    {
        fprintf(stderr, "init_video_decoder() failed!\n");
        goto end;
    }
    if (!init_audio_device(&audio_device))
    {
        fprintf(stderr, "init_audio_device() failed!\n");
        goto end;
    }
    if (!init_audio_resample(&audio_resample,
                             audio_decoder.codec_ctx->channels, audio_device.audio_spec.channels,
                             audio_decoder.codec_ctx->sample_fmt, AV_SAMPLE_FMT_S16,
                             audio_decoder.codec_ctx->channel_layout, AV_CH_LAYOUT_STEREO,
                             audio_decoder.codec_ctx->sample_rate, audio_device.audio_spec.freq))
    {
        fprintf(stderr, "init_audio_resample() failed!\n");
        goto end;
    }
    if (!init_video_device(&video_device, WINDOW_ORIG_X, WINDOW_ORIG_Y, WINDOW_WIDTH, WINDOW_HEIGHT))
    {
        fprintf(stderr, "init_video_device() failed!\n");
        goto end;
    }
    if (!init_video_rescale(&video_rescale,
                            video_decoder.codec_ctx->width, video_decoder.codec_ctx->height, video_decoder.codec_ctx->pix_fmt,
                            WINDOW_WIDTH, WINDOW_HEIGHT, AV_PIX_FMT_RGB24))
    {
        fprintf(stderr, "init_video_rescale() failed!\n");
        goto end;
    }
    if (!packet_queue_init(&audio_queue))
    {
        fprintf(stderr, "Couldn't initialize audio queue!\n");
        goto end;
    }

    if (!packet_queue_init(&video_queue))
    {
        fprintf(stderr, "Couldn't initialize video queue!\n");
        goto end;
    }

end:
    // 退出SDL
    SDL_Quit();
    // 释放媒体容器
    release_media_container(&media_container);
    return -1;
}