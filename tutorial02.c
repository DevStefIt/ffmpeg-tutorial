// tutorial02.c
// A pedagogical video player that will stream through every video frame as fast as it can.
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
// tutorial02 myvideofile.mpg
//
// to play the video stream on your screen.

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>

#undef main
int main(int argc, char *argv[])
{
  if (argc < 2)
  {
      fprintf(stderr, "Please provide a movie file\n");
      exit(1);
  }

  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
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

  AVCodec *pCodec = NULL;
  AVCodecParameters *pCodecParams = NULL;

  // Find the first video stream
  int videoStream = -1;
  int i;

  for (i = 0; i < pFormatCtx->nb_streams; i++)
  {
    if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
      videoStream = i;
      pCodecParams = pFormatCtx->streams[i]->codecpar;
      pCodec = avcodec_find_decoder(pCodecParams->codec_id);
      break; // We want only the first video stream, the leave the other stream that might be present in the file
    }
  }

  // Get a pointer to the codec context for the video stream
  AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
  if (pCodecCtx == NULL)
  {
    fprintf(stderr, "Could not allocate video codec context");
    return -1;
  }

  // Fill the codec context from the codec parameters values
  if (avcodec_parameters_to_context(pCodecCtx, pCodecParams) < 0)
  {
    fprintf(stderr, "Failed to copy codec params to codec context");
    return -1;
  }

  if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
  {
    fprintf(stderr, "failed to open codec");
    return -1;
  }

  // Allocate video frame
  AVFrame *pFrame = av_frame_alloc();
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

  AVFrame *pFrameYUV = av_frame_alloc();
  if(pFrameYUV == NULL)
  {
    fprintf(stderr, "Could not allocate output video frame");
    return -1;
  }

  // Make a screen to put our video
  // #ifndef __DARWIN__
  //     screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 0, 0);
  // #else
  //     screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 24, 0);
  // #endif
  //     if (!screen)
  //     {
  //         fprintf(stderr, "SDL: could not set video mode - exiting\n");
  //         exit(1);
  //     }

  SDL_Window *window = SDL_CreateWindow("tutorial02", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, pCodecCtx->width, pCodecCtx->height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

  if (!window)
  {
      fprintf(stderr, "SDL: could not create SDL window - %s exiting\n", SDL_GetError());
      exit(1);
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
  SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
  SDL_Rect rect;
  SDL_Event event;
  // Allocate a place to put our YUV image on that screen
  // bmp = SDL_CreateYUVOverlay(pCodecCtx->width,
  //                            pCodecCtx->height,
  //                            SDL_YV12_OVERLAY,
  //                            screen);

  struct SwsContext *swsCtx =
      sws_getContext
      (
          pCodecCtx->width,
          pCodecCtx->height,
          pCodecCtx->pix_fmt,
          pCodecCtx->width,
          pCodecCtx->height,
          AV_PIX_FMT_YUV420P,
          SWS_BILINEAR,
          NULL,
          NULL,
          NULL
      );

  // Read frames and save first five frames to disk
  i = 0;
  while(av_read_frame(pFormatCtx, pPacket) >=0 )
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
        // SDL_LockYUVOverlay(bmp);

        // AVPicture pict;
        // pict.data[0] = bmp->pixels[0];
        // pict.data[1] = bmp->pixels[2];
        // pict.data[2] = bmp->pixels[1];

        // pict.linesize[0] = bmp->pitches[0];
        // pict.linesize[1] = bmp->pitches[2];
        // pict.linesize[2] = bmp->pitches[1];

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
