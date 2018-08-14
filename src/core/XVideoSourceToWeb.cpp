#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mutex>
#include <chrono>
#include <iostream>
#include <fstream>
#include <string>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <netinet/tcp.h>

// If we have C++14, then shared_timed_mutex is a better option for BufferGuard,
// so it could allow one writer and multiple readers. However mongoose web server
// is single threaded anyway, so we'll live with normal mutex for now.
// shared_lock<shared_timed_mutex> for readers and
// unique_lock<shared_timed_mutex> for writer
// #include <shared_mutex>

#include "XVideoSourceToWeb.hpp"
#include "XJpegEncoder.hpp"

using namespace std;
using namespace std::chrono;

int file_name = 0;

namespace Private
{
#define JPEG_BUFFER_SIZE (1024 * 1024)

// Listener for video source events
class VideoListener : public IVideoSourceListener
{
  private:
    XVideoSourceToWebData *Owner;
  public:
    VideoListener(XVideoSourceToWebData *owner) : Owner(owner) {}
    void OnNewImage(const shared_ptr<const XImage> &image);
    void OnError(const string &errorMessage, bool fatal);
};

// Web request handler providing camera images as JPEGs
class JpegRequestHandler : public IWebRequestHandler
{
  private:
    XVideoSourceToWebData *Owner;
  public:
    JpegRequestHandler(const string &uri, XVideoSourceToWebData *owner) : IWebRequestHandler(uri, false), Owner(owner)
    {
    }
    void HandleHttpRequest(const IWebRequest &request, IWebResponse &response);
};

// Web request handler providing camera images as MJPEG stream
class MjpegRequestHandler : public IWebRequestHandler
{
  private:
    XVideoSourceToWebData *Owner;
    uint32_t FrameInterval;
  public:
    MjpegRequestHandler(const string &uri, uint32_t frameRate, XVideoSourceToWebData *owner) : IWebRequestHandler(uri, false), Owner(owner), FrameInterval(1000 / frameRate)
    {
    }
    void HandleHttpRequest(const IWebRequest &request, IWebResponse &response);
    void HandleTimer(IWebResponse &response);
};

// Private implementation details for the XVideoSourceToWeb
class XVideoSourceToWebData
{
  public:
    volatile bool NewImageAvailable;
    volatile bool VideoSourceError;
    XError InternalError;
    uint8_t *JpegBuffer;
    uint32_t JpegBufferSize;
    uint32_t JpegSize;
    VideoListener VideoSourceListener;
    shared_ptr<XImage> CameraImage;
    string VideoSourceErrorMessage;
    mutex ImageGuard;
    mutex BufferGuard;
    XJpegEncoder JpegEncoder;
    int handle;

  public:
    XVideoSourceToWebData(uint16_t jpegQuality) : NewImageAvailable(false), VideoSourceError(false), InternalError(XError::Success),
                                                  JpegBuffer(nullptr), JpegBufferSize(0), JpegSize(0), VideoSourceListener(this),
                                                  CameraImage(), VideoSourceErrorMessage(), ImageGuard(), BufferGuard(),
                                                  JpegEncoder(jpegQuality, true)
    {
        handle = -1;
        // allocate initial buffer for JPEG images
        JpegBuffer = (uint8_t *)malloc(JPEG_BUFFER_SIZE);
        if (JpegBuffer != nullptr)
        {
            JpegBufferSize = JPEG_BUFFER_SIZE;
        }
    }

    ~XVideoSourceToWebData()
    {
        if (JpegBuffer != nullptr)
        {
            free(JpegBuffer);
        }
    }

    bool IsError();
    void ReportError(IWebResponse &response);
    void EncodeCameraImage();
    string timeNow();
             
};
} // namespace Private


XVideoSourceToWeb::XVideoSourceToWeb(uint16_t jpegQuality) : mData(new Private::XVideoSourceToWebData(jpegQuality))
{
}

XVideoSourceToWeb::~XVideoSourceToWeb()
{
    delete mData;
}

// Get video source listener, which could be fed to some video source
IVideoSourceListener *XVideoSourceToWeb::VideoSourceListener() const
{
    return &mData->VideoSourceListener;
}

// Create web request handler to provide camera images as JPEGs
shared_ptr<IWebRequestHandler> XVideoSourceToWeb::CreateJpegHandler(const string &uri) const
{
    return make_shared<Private::JpegRequestHandler>(uri, mData);
}

// Create web request handler to provide camera images as MJPEG stream
shared_ptr<IWebRequestHandler> XVideoSourceToWeb::CreateMjpegHandler(const string &uri, uint32_t frameRate) const
{
    return make_shared<Private::MjpegRequestHandler>(uri, frameRate, mData);
}

// Get/Set JPEG quality (valid only if camera provides uncompressed images)
uint16_t XVideoSourceToWeb::JpegQuality() const
{
    return mData->JpegEncoder.Quality();
}
void XVideoSourceToWeb::SetJpegQuality(uint16_t quality)
{
    mData->JpegEncoder.SetQuality(quality);
}

namespace Private
{
int handle = -1;
int conRet = -1;
unsigned int a = 35;
unsigned int b = 163;
unsigned int c = 144;
unsigned int d = 7;
unsigned short port = 9000;
unsigned int address = (a << 24) |
                       (b << 16) |
                       (c << 8) |
                       d;
uint32_t imgSize = 0;
int ctr = 0;

// On new image from video source - make a copy of it
void VideoListener::OnNewImage(const shared_ptr<const XImage> &image)
{
    // lock_guard<mutex> lock(Owner->ImageGuard);   Commemted by Murali
    Owner->InternalError = image->CopyDataOrClone(Owner->CameraImage);
    if (Owner->InternalError == XError::Success)
    {
        Owner->NewImageAvailable = true;
    }
    else
    {
        printf("OnNewImage NewImageAvailable is false \n");
    }
    // since we got an image from video source, clear any error reported by it
    Owner->VideoSourceErrorMessage.clear();
    Owner->VideoSourceError = false;
    // New Code
    if (handle == -1)
    {
        handle = socket(AF_INET, SOCK_STREAM, 0);
        if (handle <= 0)
        {
            printf("failed to create socket\n");
            return;
        }
        printf("OnNewImage - Socket Handle created \n");
        int optval = 1;
        if (setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(int)) < 0)
            printf("Cannot set TCP_NODELAY option ");
        }
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(address);
    addr.sin_port = htons(port);
    if (conRet == -1)
    {
        conRet = connect(handle, (struct sockaddr *)&addr, sizeof(addr));
    }
    if (conRet < 0)
        printf("Error while connecting");
    uint32_t handlingTime = 0;
    if (!Owner->IsError())
    {
        //steady_clock::time_point startTime = steady_clock::now();
        Owner->EncodeCameraImage();
        //handlingTime = static_cast<uint32_t>(duration_cast<std::chrono::milliseconds>(steady_clock::now() - startTime).count());
        //cout << "New Image Encoding time : " << handlingTime << "\n";
    }
    steady_clock::time_point startTime = steady_clock::now();
    imgSize = Owner->JpegSize;
    ctr++;

    int sent_bytes = write(handle, &imgSize, sizeof(uint32_t));
    int sent_bytes2 = write(handle, (const char *)Owner->JpegBuffer, imgSize);
    // printf("packet sizes %d %d \n", sent_bytes, sent_bytes2);
    /*
    if (sent_bytes2 != imgSize)
    {
        printf("failed to send packet Error Code %d", errno);
        return;
    }
    */
    // handlingTime = static_cast<uint32_t>(duration_cast<std::chrono::milliseconds>(steady_clock::now() - startTime).count());
    // cout << "Sending time : " << handlingTime << "\n";
    cout << Owner->timeNow() << "\n";
}

// An error coming from video source
void VideoListener::OnError(const string &errorMessage, bool /* fatal */)
{
    lock_guard<mutex> imageLock(Owner->ImageGuard);
    printf("Error in Listener \n");
    Owner->VideoSourceErrorMessage = errorMessage;
    Owner->VideoSourceError = true;
}

// Handle JPEG request - provide current camera image
void JpegRequestHandler::HandleHttpRequest(const IWebRequest & /* request */, IWebResponse &response)
{
    if (!Owner->IsError())
    {
        Owner->EncodeCameraImage();
    }
    if (Owner->IsError())
    {
        Owner->ReportError(response);
    }
    else
    {
        lock_guard<mutex> lock(Owner->BufferGuard);

        if (Owner->JpegSize == 0)
        {
            response.SendError(500, "No image from video source");
        }
        else
        {
            response.Printf("HTTP/1.1 200 OK\r\n"
                            "Content-Type: image/jpeg\r\n"
                            "Content-Length: %u\r\n"
                            "Cache-Control: no-store, must-revalidate\r\nPragma: no-cache\r\nExpires: 0\r\n"
                            "\r\n",
                            Owner->JpegSize);

            response.Send(Owner->JpegBuffer, Owner->JpegSize);
            cout << "J Owner->JpegSize : " << Owner->JpegSize << "\n";
        }
    }
}

// Handle MJPEG request - continuously provide camera images as MJPEG stream
void MjpegRequestHandler::HandleHttpRequest(const IWebRequest & /* request */, IWebResponse &response)
{
    uint32_t handlingTime = 0;
    if (!Owner->IsError())
    {
        steady_clock::time_point startTime = steady_clock::now();
        Owner->EncodeCameraImage();
        handlingTime = static_cast<uint32_t>(duration_cast<std::chrono::milliseconds>(steady_clock::now() - startTime).count());
    }
    if (Owner->IsError())
    {
        Owner->ReportError(response);
    }
    else
    {
        steady_clock::time_point startTime = steady_clock::now();
        lock_guard<mutex> lock(Owner->BufferGuard);

        if (Owner->JpegSize == 0)
        {
            response.SendError(500, "No image from video source");
        }
        else
        {
            // provide first image of the MJPEG stream
            response.Printf("HTTP/1.1 200 OK\r\n"
                            "Cache-Control: no-store, must-revalidate\r\nPragma: no-cache\r\nExpires: 0\r\n"
                            "Connection: close\r\n"
                            "Content-Type: multipart/x-mixed-replace; boundary=--myboundary\r\n"
                            "\r\n");

            response.Printf("--myboundary\r\n"
                            "Content-Type: image/jpeg\r\n"
                            "Content-Length: %u\r\n"
                            "\r\n",
                            Owner->JpegSize);

            response.Send(Owner->JpegBuffer, Owner->JpegSize);
            cout << "M Owner->JpegSize : " << Owner->JpegSize << "\n";

            // get final request handling time
            handlingTime += static_cast<uint32_t>(duration_cast<std::chrono::milliseconds>(steady_clock::now() - startTime).count());

            // set time to provide next images
            response.SetTimer(FrameInterval);
        }
    }
}

// Timer event for then connection handling MJPEG request - provide new image
void MjpegRequestHandler::HandleTimer(IWebResponse &response)
{

    if (handle == -1)
    {
        handle = socket(AF_INET, SOCK_STREAM, 0);
        if (handle <= 0)
        {
            printf("failed to create socket\n");
            return;
        }
        printf("HandleTimer - Socket Handle created \n");
    }
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(address);
    addr.sin_port = htons(port);
    printf("HandleTimer - IP and Port %ul %d\n", address, port);
    if (conRet == -1)
    {
        conRet = connect(handle, (struct sockaddr *)&addr, sizeof(addr));
    }
    if (conRet < 0)
        printf("Error while connecting");

    uint32_t handlingTime = 0;
    if (!Owner->IsError())
    {
        steady_clock::time_point startTime = steady_clock::now();
        Owner->EncodeCameraImage();
        handlingTime = static_cast<uint32_t>(duration_cast<std::chrono::milliseconds>(steady_clock::now() - startTime).count());
    }

    if ((Owner->IsError()) || (Owner->JpegSize == 0))
    {
        response.CloseConnection();
    }
    else
    {
        steady_clock::time_point startTime = steady_clock::now();
        lock_guard<mutex> lock(Owner->BufferGuard);

        // don't try sending too much on slow connections - it will only create video lag
        cout << "HandleTimer - ToSendLength JpegSize " << response.ToSendDataLength() << " " << Owner->JpegSize << "\n";

        if (response.ToSendDataLength() < 2 * Owner->JpegSize)
        {
            cout << "HandleTimer - HH Owner->JpegSize : " << Owner->JpegSize << "\n";
            imgSize = Owner->JpegSize;
            ctr++;
            int sent_bytes = write(handle, &imgSize, sizeof(uint32_t));
            int sent_bytes2 = write(handle, (const char *)Owner->JpegBuffer, imgSize);
            printf("packet sizes %d %d \n", sent_bytes, sent_bytes2);
            if (sent_bytes2 != imgSize)
            {
                printf("failed to send packet Error Code %d", errno);
                return;
            }
        }
        // get final request handling time
        handlingTime += static_cast<uint32_t>(duration_cast<std::chrono::milliseconds>(steady_clock::now() - startTime).count());
        // set new timer for further images
        response.SetTimer((handlingTime >= FrameInterval) ? 1 : FrameInterval - handlingTime);
    }
}

// Check if any errors happened
bool XVideoSourceToWebData::IsError()
{
    return ((InternalError != XError::Success) || (VideoSourceError));
}

// Report an error as HTTP response
void XVideoSourceToWebData::ReportError(IWebResponse &response)
{
    if (InternalError != XError::Success)
    {
        response.SendError(500, InternalError.ToString().c_str());
    }
    else if (VideoSourceError)
    {
        lock_guard<mutex> imageLock(ImageGuard);
        response.SendError(500, VideoSourceErrorMessage.c_str());
    }
}

// Encode current camera image as JPEG
void XVideoSourceToWebData::EncodeCameraImage()
{
    if (NewImageAvailable)
    {
        lock_guard<mutex> imageLock(ImageGuard);
        lock_guard<mutex> bufferLock(BufferGuard);
        if (JpegBuffer == nullptr)
        {
            InternalError = XError::OutOfMemory;
        }
        else
        {
            if (CameraImage->Format() == XPixelFormat::JPEG)
            {
                // cout << "Inside If 1 \n";
                // check allocated buffer size
                if (JpegBufferSize < static_cast<uint32_t>(CameraImage->Width()))
                {
                    // cout << "Inside If 2 \n";
                    // make new size 10% bigger than needed
                    uint32_t newSize = CameraImage->Width() + CameraImage->Width() / 10;
                    JpegBuffer = (uint8_t *)realloc(JpegBuffer, newSize);
                    if (JpegBuffer != nullptr)
                    {
                        JpegBufferSize = newSize;
                    }
                    else
                    {
                        InternalError = XError::OutOfMemory;
                    }
                }
                if (JpegBuffer != nullptr)
                {
                    // cout << "Inside If 3 \n";
                    // just copy JPEG data if we got already encoded image
                    memcpy(JpegBuffer, CameraImage->Data(), CameraImage->Width());
                    JpegSize = CameraImage->Width();
                }
            }
            else
            {
                // encode image as JPEG (buffer is re-allocated if too small by encoder)
                JpegSize = JpegBufferSize;
                InternalError = JpegEncoder.EncodeToMemory(CameraImage, &JpegBuffer, &JpegSize);
            }
        }
        NewImageAvailable = false;
    }
}
string XVideoSourceToWebData::timeNow() {
    time_t t = time(0);
    char buffer[9] = {0};

    strftime(buffer, 9, "%H:%M:%S", localtime(&t));
    return string(buffer);
}

} // namespace Private
