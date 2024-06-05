// tutorial03.c
// A pedagogical video player that will stream through every video frame as fast as it can
// and play audio (out of sync).
//
// This tutorial was written by Stephen Dranger (dranger@gmail.com).
//
// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard,
// and a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
//
// Use the Makefile to build all examples.
//
// Run using
// tutorial03 myvideofile.mpg
//
// to play the stream on your screen.

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

typedef struct PacketQueue
{
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;

PacketQueue audioq;

int quit = 0;

void packet_queue_init(PacketQueue *q)
{
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}
int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{

  AVPacketList *pkt1;
  if (av_dup_packet(pkt) < 0)
  {
    return -1;
  }
  pkt1 = av_malloc(sizeof(AVPacketList));
  if (!pkt1)
    return -1;
  pkt1->pkt = *pkt;
  pkt1->next = NULL;

  SDL_LockMutex(q->mutex);

  if (!q->last_pkt)
    q->first_pkt = pkt1;
  else
    q->last_pkt->next = pkt1;
  q->last_pkt = pkt1;
  q->nb_packets++;
  q->size += pkt1->pkt.size;
  SDL_CondSignal(q->cond);

  SDL_UnlockMutex(q->mutex);
  return 0;
}
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
  AVPacketList *pkt1;
  int ret;

  SDL_LockMutex(q->mutex);

  for (;;)
  {

    if (quit)
    {
      ret = -1;
      break;
    }

    pkt1 = q->first_pkt;
    if (pkt1)
    {
      q->first_pkt = pkt1->next;
      if (!q->first_pkt)
        q->last_pkt = NULL;
      q->nb_packets--;
      q->size -= pkt1->pkt.size;
      *pkt = pkt1->pkt;
      av_free(pkt1);
      ret = 1;
      break;
    }
    else if (!block)
    {
      ret = 0;
      break;
    }
    else
    {
      SDL_CondWait(q->cond, q->mutex);
    }
  }
  SDL_UnlockMutex(q->mutex);
  return ret;
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size)
{
  static AVPacket pkt;
  static uint8_t *audio_pkt_data = NULL;
  static int audio_pkt_size = 0;
  static AVFrame frame;

  int len1, data_size = 0;

  for (;;)
  {
    while (audio_pkt_size > 0)
    {
      int got_frame = 0;
      len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
      if (len1 < 0)
      {
        /* if error, skip frame */
        audio_pkt_size = 0;
        break;
      }
      audio_pkt_data += len1;
      audio_pkt_size -= len1;
      if (got_frame)
      {
        data_size =
            av_samples_get_buffer_size(
                NULL,
                aCodecCtx->channels,
                frame.nb_samples,
                aCodecCtx->sample_fmt,
                1);
        memcpy(audio_buf, frame.data[0], data_size);
      }
      if (data_size <= 0)
      {
        /* No data yet, get more frames */
        continue;
      }
      /* We have data, return it and come back for more later */
      return data_size;
    }
    if (pkt.data)
      av_free_packet(&pkt);

    if (quit)
    {
      return -1;
    }

    if (packet_queue_get(&audioq, &pkt, 1) < 0)
    {
      return -1;
    }
    audio_pkt_data = pkt.data;
    audio_pkt_size = pkt.size;
  }
}

void audio_callback(void *userdata, Uint8 *stream, int len)
{

  AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
  int len1, audio_size;

  static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
  static unsigned int audio_buf_size = 0;
  static unsigned int audio_buf_index = 0;

  while (len > 0)
  {
    if (audio_buf_index >= audio_buf_size)
    {
      /* We have already sent all our data; get more */
      audio_size = audio_decode_frame(aCodecCtx, audio_buf, audio_buf_size);
      if (audio_size < 0)
      {
        /* If error, output silence */
        audio_buf_size = 1024; // arbitrary?
        memset(audio_buf, 0, audio_buf_size);
      }
      else
      {
        audio_buf_size = audio_size;
      }
      audio_buf_index = 0;
    }
    len1 = audio_buf_size - audio_buf_index;
    if (len1 > len)
      len1 = len;
    // memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
    // SDL_MIX_MAXVOLUME 可用于音量控制
    SDL_MixAudio(stream, (uint8_t *)audio_buf + audio_buf_index, len1, SDL_MIX_MAXVOLUME);
    len -= len1;
    stream += len1;
    audio_buf_index += len1;
  }
}

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
      fprintf(stderr, "Please provide a movie file\n");
      exit(1);
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
  {
    fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
    exit(1);
  }

  // Open video file
  AVFormatContext *pFormatCtx = avformat_alloc_context();
  if (!pFormatCtx)
  {
      fprintf(stderr, "Could not allocate memory for format context");
      return -1;
  }

  if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0)
  {
      fprintf(stderr, "Cannot open file");
      return -1;
  }

  // Retrieve stream information
  if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
  {
    fprintf(stderr, "Could not find stream information");
    return -1; // Couldn't find stream information
  }

  // Dump information about file onto standard error
  av_dump_format(pFormatCtx, 0, argv[1], 0);

  AVCodec *pCodec = NULL;
  AVCodecParameters *pCodecParams = NULL;

  // Find the first video stream
  int videoStream = -1;
  int audioStream = -1;

  int i;
  for (i = 0; i < pFormatCtx->nb_streams; i++)
  {
    if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
        videoStream < 0)
    {
      videoStream = i;
    }
    if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
        audioStream < 0)
    {
      audioStream = i;
    }
  }
  if (videoStream == -1)
  {
    fprintf(stderr, "Could not find any video stream");
    return -1;
  }
  if (audioStream == -1)
  {
    fprintf(stderr, "Could not find any audio stream");
    return -1;
  }
  
  AVCodecParameters *aCodecParams = pFormatCtx->streams[audioStream]->codecpar;
  AVCodec *aCodec = avcodec_find_decoder(pCodecParams->codec_id);
  if (!aCodec)
  {
    fprintf(stderr, "Unsupported audio codec.\n");
    return -1;
  }

  // Set audio settings from codec info
  SDL_AudioSpec wanted_spec;
  wanted_spec.freq = aCodecParams->sample_rate;
  wanted_spec.format = AUDIO_S16SYS;
  wanted_spec.channels = aCodecParams->channels;
  wanted_spec.silence = 0;
  wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
  wanted_spec.callback = audio_callback;
  wanted_spec.userdata = aCodecParams;
  
  SDL_AudioSpec spec;
  if (SDL_OpenAudio(&wanted_spec, &spec) < 0)
  {
    fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
    return -1;
  }
  avcodec_open2(aCodecCtx, aCodec, &audioOptionsDict);

  // audio_st = pFormatCtx->streams[index]
  packet_queue_init(&audioq);
  SDL_PauseAudio(0);

  // Get a pointer to the codec context for the video stream
  AVCodecParameters *pCodecParams = NULL;
  pCodecParams = pFormatCtx->streams[videoStream]->codecpar;

  // Find the decoder for the video stream
  pCodec = avcodec_find_decoder(pCodecParams->codec_id);
  if (pCodec == NULL)
  {
    fprintf(stderr, "Unsupported video codec!\n");
    return -1; // Codec not found
  }

  pCodecCtx = avcodec_alloc_context3(pCodec);
  if (pCodecCtx == NULL)
  {
    fprintf(stderr, "Could not allocate video codec context");
    return -1;
  }

  // Open codec
  if (avcodec_open2(pCodecCtx, pCodec, &videoOptionsDict) < 0)
  {
    fprintf(stderr, "Failed to open codec");
    return -1;
  }

  // Allocate video frame
  pFrame = av_frame_alloc();
  if (pFrame == NULL)
  {
    fprintf(stderr, "Could not allocate video frame");
    return -1;
  }

  AVPacket *pPacket = av_packet_alloc();
  if (pPacket == NULL)
  {
    fprintf(stderr, "Failed to allocate memory for AVPacket");
    return -1;
  }

  pFrameYUV = av_frame_alloc();
  if(pFrameYUV == NULL)
  {
    fprintf(stderr, "Could not allocate output video frame");
    return -1;
  }

  av_image_fill_arrays(
      pFrameYUV->data,
      pFrameYUV->linesize,
      (uint8_t const *const *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1)),
      AV_PIX_FMT_YUV420P,
      pCodecCtx->width,
      pCodecCtx->height,
      1);

  // Make a screen to put our video

  // #ifndef __DARWIN__
  //         screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 0, 0);
  // #else
  //         screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 24, 0);
  // #endif
  //   if(!screen) {
  //     fprintf(stderr, "SDL: could not set video mode - exiting\n");
  //     exit(1);
  //   }
  window = SDL_CreateWindow("tutorial03", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, pCodecCtx->width, pCodecCtx->height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!window)
  {
    fprintf(stderr, "SDL: could not create SDL window - %s exiting\n", SDL_GetError());
    exit(1);
  }
  renderer = SDL_CreateRenderer(window, -1, 0);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
  
  // Allocate a place to put our YUV image on that screen
  // bmp = SDL_CreateYUVOverlay(pCodecCtx->width,
  // 			 pCodecCtx->height,
  // 			 SDL_YV12_OVERLAY,
  // 			 screen);
  struct SwsContext *swsCtx =
      sws_getContext(
          pCodecCtx->width,
          pCodecCtx->height,
          pCodecCtx->pix_fmt,
          pCodecCtx->width,
          pCodecCtx->height,
          AV_PIX_FMT_YUV420P,
          SWS_BILINEAR,
          NULL,
          NULL,
          NULL);

  // Read frames and save first five frames to disk
  i = 0;
  while (av_read_frame(pFormatCtx, pPacket) >= 0)
  {
    // Is this a packet from the video stream?
    if (pPacket->stream_index == videoStream)
    {
      // Decode video frame  
      if (avcodec_send_packet(pCodecCtx, pPacket) < 0)
      {
        fprintf(stderr, "Error while sending packet to decoder.");
        return -1;
      }

      int frameFinished = 1;
      // Did we get a video frame?
      if (frameFinished >= 0)
      {
        frameFinished = avcodec_receive_frame(pCodecCtx, pFrame);
        // These two return values are special and mean there is no output
        // frame available, but there were no errors during decoding
        if (frameFinished == AVERROR(EAGAIN) || frameFinished == AVERROR_EOF)
        {
          break;
        }
        else if (frameFinished < 0)
        {
          fprintf(stderr, "Error during decoding");
          return -1;
        }

        // Convert the image into YUV format that SDL uses
        if (frameFinished >= 0)
        {
          if (av_image_alloc(pFrameYUV->data, pFrameYUV->linesize, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, 1) < 0)
          {
            fprintf(stderr, "Could not allocate output frame");
            return -1;
          }

          // Convert the image from its native format to RGB
          sws_scale
          (
              swsCtx,
              pFrame->data,
              pFrame->linesize,
              0,
              pCodecCtx->height,
              pFrameYUV->data,
              pFrameYUV->linesize
          );

         // SDL_UnlockYUVOverlay(bmp);

          rect.x = 0;
          rect.y = 0;
          rect.w = pCodecCtx->width;
          rect.h = pCodecCtx->height;
          // SDL_DisplayYUVOverlay(bmp, &rect);

          SDL_UpdateTexture(texture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
          SDL_RenderClear(renderer);
          SDL_RenderCopy(renderer, texture, NULL, &rect);
          SDL_RenderPresent(renderer);
        }
      }
    else if (packet.stream_index == audioStream)
    {
      packet_queue_put(&audioq, &packet);
    }
    else
    {
      av_free_packet(&packet);
    }
    // Free the packet that was allocated by av_read_frame
      av_packet_unref(pPacket);
      SDL_PollEvent(&event);
      switch(event.type)
      {
        case SDL_QUIT:
            SDL_Quit();
            exit(0);
            break;
        default:
            break;
      }
  }

  sws_freeContext(swsCtx);

  // Free the YUV frame
  av_frame_free(&pFrame);
  av_frame_free(&pFrameYUV);

  // Close the codec
  avcodec_free_context(&pCodecCtx);

  // Close the video file
  avformat_close_input(&pFormatCtx);

  return 0;
}
