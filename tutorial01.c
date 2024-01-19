// tutorial01.c
//
// This tutorial was written by Stephen Dranger (dranger@gmail.com).
//
// Code based on a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1

// A small sample program that shows how to use libavformat and libavcodec to
// read video from a file.
//
// Use the Makefile to build all examples.
//
// Run using
//
// tutorial01 myvideofile.mpg
//
// to write the first five frames from "myvideofile.mpg" to disk in PPM
// format.

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <stdio.h>

void saveFrame(AVFrame *pFrame, int width, int height, int iFrame)
{
  FILE *pFile;
  char szFilename[32];
  int  y;
  
  // Open file
  sprintf(szFilename, "frame%d.ppm", iFrame);
  pFile = fopen(szFilename, "wb");
  if(pFile == NULL)
  {
    fprintf(stderr, "Cannot open file");
    return;
  }
  
  // Write header
  fprintf(pFile, "P6\n%d %d\n%d\n", width, height, 255);
  
  // Write pixel data
  for(y = 0; y < height; y++)
    fwrite(pFrame->data[0] + y * pFrame->linesize[0], 1, width * 3, pFile);
  
  // Close file
  fclose(pFile);
}

int main(int argc, char *argv[]) {  
  if (argc < 2)
  {
    printf("Please provide a movie file\n");
    return -1;
  }

  // Register all formats and codecs
  // Now not useful anymore since version 4.0
  //av_register_all();
  
  // Open video file
  AVFormatContext *pFormatCtx = avformat_alloc_context();
  if (!pFormatCtx) {
    fprintf(stderr, "Could not allocate memory for format context");
    return -1;
  }

  if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0)
  {
    fprintf(stderr, "Cannot open file");
    return -1; // Couldn't open file
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

  if (videoStream == -1)
  {
    fprintf(stderr, "Did not find a video stream");
    return -1;
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

  // Allocate an AVFrame structure
  AVFrame *pFrameRGB = av_frame_alloc();
  if(pFrameRGB == NULL)
  {
    fprintf(stderr, "Could not allocate output video frame");
    return -1;
  }

  int frames_to_process = 5;
  i = 0;
  
  // Read frames and save first five frames to disk
  while (av_read_frame(pFormatCtx, pPacket) >= 0 && i < frames_to_process)
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
      while (frameFinished >= 0 && i < frames_to_process) 
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

        if (frameFinished >= 0)
        {
          if (av_image_alloc(pFrameRGB->data, pFrameRGB->linesize, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24, 1) < 0)
          {
            fprintf(stderr, "Could not allocate output frame");
            return -1;
          }

          struct SwsContext *swsCtx =
          sws_getContext
          (
              pCodecCtx->width,
              pCodecCtx->height,
              pCodecCtx->pix_fmt,
              pCodecCtx->width,
              pCodecCtx->height,
              AV_PIX_FMT_RGB24,
              SWS_BILINEAR,
              NULL,
              NULL,
              NULL
          );

          // Convert the image from its native format to RGB
          sws_scale
          (
              swsCtx,
              pFrame->data,
              pFrame->linesize,
              0,
              pCodecCtx->height,
              pFrameRGB->data,
              pFrameRGB->linesize
          );
          sws_freeContext(swsCtx);

          for (;i < frames_to_process; ++i)
          {
	          // Save the frame to disk
	          saveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, i);
          }
        }
      }
    }

    // Free the packet that was allocated by av_read_frame
    av_packet_unref(pPacket);
  }

  // Free the YUV frame
  av_frame_free(&pFrame);

  // Free the RGB image
  av_frame_free(&pFrameRGB);

  // Close the codec
  avcodec_free_context(&pCodecCtx);

  // Close the video file
  avformat_close_input(&pFormatCtx);
  
  return 0;
}