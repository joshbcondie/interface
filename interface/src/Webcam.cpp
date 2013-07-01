//
//  Webcam.cpp
//  interface
//
//  Created by Andrzej Kapolka on 6/17/13.
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.

#include <QTimer>
#include <QtDebug>

#include <Log.h>
#include <SharedUtil.h>

#ifdef __APPLE__
#include <UVCCameraControl.hpp>
#endif

#include "Application.h"
#include "Webcam.h"

using namespace cv;
using namespace std;
using namespace xn;

// register OpenCV matrix type with Qt metatype system
int matMetaType = qRegisterMetaType<Mat>("cv::Mat");
int rotatedRectMetaType = qRegisterMetaType<RotatedRect>("cv::RotatedRect");

Webcam::Webcam() : _enabled(false), _active(false), _frameTextureID(0), _depthTextureID(0) {
    // the grabber simply runs as fast as possible
    _grabber = new FrameGrabber();
    _grabber->moveToThread(&_grabberThread);
}

void Webcam::setEnabled(bool enabled) {
    if (_enabled == enabled) {
        return;
    }
    if ((_enabled = enabled)) {
        _grabberThread.start();
        _startTimestamp = 0;
        _frameCount = 0;
        
        // let the grabber know we're ready for the first frame
        QMetaObject::invokeMethod(_grabber, "reset");
        QMetaObject::invokeMethod(_grabber, "grabFrame");
    
    } else {
        _grabberThread.quit();
        _active = false;
    }
}

void Webcam::reset() {
    _initialFaceRect = RotatedRect();
    
    if (_enabled) {
        // send a message to the grabber
        QMetaObject::invokeMethod(_grabber, "reset");
    }
}

void Webcam::renderPreview(int screenWidth, int screenHeight) {
    if (_enabled && _frameTextureID != 0) {
        glBindTexture(GL_TEXTURE_2D, _frameTextureID);
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
            const int PREVIEW_HEIGHT = 200;
            int previewWidth = _frameWidth * PREVIEW_HEIGHT / _frameHeight;
            int top = screenHeight - 600;
            int left = screenWidth - previewWidth - 10;
            
            glTexCoord2f(0, 0);
            glVertex2f(left, top);
            glTexCoord2f(1, 0);
            glVertex2f(left + previewWidth, top);
            glTexCoord2f(1, 1);
            glVertex2f(left + previewWidth, top + PREVIEW_HEIGHT);
            glTexCoord2f(0, 1);
            glVertex2f(left, top + PREVIEW_HEIGHT);
        glEnd();
        
        if (_depthTextureID != 0) {
            glBindTexture(GL_TEXTURE_2D, _depthTextureID);
            glBegin(GL_QUADS);
                int depthPreviewWidth = _depthWidth * PREVIEW_HEIGHT / _depthHeight;
                int depthLeft = screenWidth - depthPreviewWidth - 10;
                glTexCoord2f(0, 0);
                glVertex2f(depthLeft, top - PREVIEW_HEIGHT);
                glTexCoord2f(1, 0);
                glVertex2f(depthLeft + depthPreviewWidth, top - PREVIEW_HEIGHT);
                glTexCoord2f(1, 1);
                glVertex2f(depthLeft + depthPreviewWidth, top);
                glTexCoord2f(0, 1);
                glVertex2f(depthLeft, top);
            glEnd();
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        
        glBegin(GL_LINE_LOOP);
            Point2f facePoints[4];
            _faceRect.points(facePoints);
            float xScale = previewWidth / (float)_frameWidth;
            float yScale = PREVIEW_HEIGHT / (float)_frameHeight;
            glVertex2f(left + facePoints[0].x * xScale, top + facePoints[0].y * yScale);
            glVertex2f(left + facePoints[1].x * xScale, top + facePoints[1].y * yScale);
            glVertex2f(left + facePoints[2].x * xScale, top + facePoints[2].y * yScale);
            glVertex2f(left + facePoints[3].x * xScale, top + facePoints[3].y * yScale);
        glEnd();
        
        char fps[20];
        sprintf(fps, "FPS: %d", (int)(roundf(_frameCount * 1000000.0f / (usecTimestampNow() - _startTimestamp))));
        drawtext(left, top + PREVIEW_HEIGHT + 20, 0.10, 0, 1, 0, fps);
    }
}

Webcam::~Webcam() {
    // stop the grabber thread
    _grabberThread.quit();
    _grabberThread.wait();
    
    delete _grabber;
}

void Webcam::setFrame(const Mat& frame, int format, const Mat& depth, const RotatedRect& faceRect) {
    IplImage image = frame;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, image.widthStep / 3);
    if (_frameTextureID == 0) {
        glGenTextures(1, &_frameTextureID);
        glBindTexture(GL_TEXTURE_2D, _frameTextureID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, _frameWidth = image.width, _frameHeight = image.height, 0, format,
            GL_UNSIGNED_BYTE, image.imageData);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        printLog("Capturing video at %dx%d.\n", _frameWidth, _frameHeight);
    
    } else {
        glBindTexture(GL_TEXTURE_2D, _frameTextureID);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _frameWidth, _frameHeight, format, GL_UNSIGNED_BYTE, image.imageData);
    }
    
    if (!depth.empty()) {
        IplImage depthImage = depth;
        glPixelStorei(GL_UNPACK_ROW_LENGTH, depthImage.widthStep);
        if (_depthTextureID == 0) {
            glGenTextures(1, &_depthTextureID);
            glBindTexture(GL_TEXTURE_2D, _depthTextureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, _depthWidth = depthImage.width, _depthHeight = depthImage.height, 0,
                GL_LUMINANCE, GL_UNSIGNED_BYTE, depthImage.imageData);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            printLog("Capturing depth at %dx%d.\n", _depthWidth, _depthHeight);
            
        } else {
            glBindTexture(GL_TEXTURE_2D, _depthTextureID);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _depthWidth, _depthHeight, GL_LUMINANCE,
                GL_UNSIGNED_BYTE, depthImage.imageData);        
        }
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    // store our face rect, update our frame count for fps computation
    _faceRect = faceRect;
    _frameCount++;
    
    const int MAX_FPS = 60;
    const int MIN_FRAME_DELAY = 1000000 / MAX_FPS;
    long long now = usecTimestampNow();
    long long remaining = MIN_FRAME_DELAY;
    if (_startTimestamp == 0) {
        _startTimestamp = now;
    } else {
        remaining -= (now - _lastFrameTimestamp);
    }
    _lastFrameTimestamp = now;
    
    // roll is just the angle of the face rect (correcting for 180 degree rotations)
    float roll = faceRect.angle;
    if (roll < -90.0f) {
        roll += 180.0f;
        
    } else if (roll > 90.0f) {
        roll -= 180.0f;
    }
    const float ROTATION_SMOOTHING = 0.95f;
    _estimatedRotation.z = glm::mix(roll, _estimatedRotation.z, ROTATION_SMOOTHING);
    
    // determine position based on translation and scaling of the face rect
    if (_initialFaceRect.size.area() == 0) {
        _initialFaceRect = faceRect;
        _estimatedPosition = glm::vec3();
    
    } else {
        float proportion = sqrtf(_initialFaceRect.size.area() / (float)faceRect.size.area());
        const float DISTANCE_TO_CAMERA = 0.333f;
        const float POSITION_SCALE = 0.5f;
        float z = DISTANCE_TO_CAMERA * proportion - DISTANCE_TO_CAMERA;
        glm::vec3 position = glm::vec3(
            (faceRect.center.x - _initialFaceRect.center.x) * proportion * POSITION_SCALE / _frameWidth,
            (faceRect.center.y - _initialFaceRect.center.y) * proportion * POSITION_SCALE / _frameWidth,
            z);
        const float POSITION_SMOOTHING = 0.95f;
        _estimatedPosition = glm::mix(position, _estimatedPosition, POSITION_SMOOTHING);
    }
    
    // note that we have data
    _active = true;
    
    // let the grabber know we're ready for the next frame
    QTimer::singleShot(qMax((int)remaining / 1000, 0), _grabber, SLOT(grabFrame()));
}

FrameGrabber::FrameGrabber() : _initialized(false), _capture(0), _searchWindow(0, 0, 0, 0) {
}

FrameGrabber::~FrameGrabber() {
    if (_capture != 0) {
        cvReleaseCapture(&_capture);
    }
}

void FrameGrabber::reset() {
    _searchWindow = cv::Rect(0, 0, 0, 0);
}

void FrameGrabber::grabFrame() {
    if (!(_initialized || init())) {
        return;
    }
    int format = GL_BGR;
    Mat frame;
    
#ifdef HAVE_OPENNI
    if (_depthGenerator.IsValid()) {
        _xnContext.WaitAnyUpdateAll();
        frame = Mat(_imageMetaData.YRes(), _imageMetaData.XRes(), CV_8UC3, (void*)_imageGenerator.GetImageMap());
        format = GL_RGB;
        
        Mat depth = Mat(_depthMetaData.YRes(), _depthMetaData.XRes(), CV_16UC1, (void*)_depthGenerator.GetDepthMap());
        depth.convertTo(_grayDepthFrame, CV_8UC1, 256.0 / 2048.0);
    }
#endif

    if (frame.empty()) {
        IplImage* image = cvQueryFrame(_capture);
        if (image == 0) {
            // try again later
            QMetaObject::invokeMethod(this, "grabFrame", Qt::QueuedConnection);
            return;
        }
        // make sure it's in the format we expect
        if (image->nChannels != 3 || image->depth != IPL_DEPTH_8U || image->dataOrder != IPL_DATA_ORDER_PIXEL ||
                image->origin != 0) {
            printLog("Invalid webcam image format.\n");
            return;
        }
        frame = image;
    }
    
    // if we don't have a search window (yet), try using the face cascade
    int channels = 0;
    float ranges[] = { 0, 180 };
    const float* range = ranges;
    if (_searchWindow.area() == 0) {
        vector<cv::Rect> faces;
        _faceCascade.detectMultiScale(frame, faces, 1.1, 6);
        if (!faces.empty()) {
            _searchWindow = faces.front();
            updateHSVFrame(frame, format);
        
            Mat faceHsv(_hsvFrame, _searchWindow);
            Mat faceMask(_mask, _searchWindow);
            int sizes = 30;
            calcHist(&faceHsv, 1, &channels, faceMask, _histogram, 1, &sizes, &range);
            double min, max;
            minMaxLoc(_histogram, &min, &max);
            _histogram.convertTo(_histogram, -1, (max == 0.0) ? 0.0 : 255.0 / max);
        }
    }
    RotatedRect faceRect;
    if (_searchWindow.area() > 0) {
        updateHSVFrame(frame, format);
        
        calcBackProject(&_hsvFrame, 1, &channels, _histogram, _backProject, &range);
        bitwise_and(_backProject, _mask, _backProject);
        
        faceRect = CamShift(_backProject, _searchWindow, TermCriteria(CV_TERMCRIT_EPS | CV_TERMCRIT_ITER, 10, 1));
        _searchWindow = faceRect.boundingRect();
    }   
    QMetaObject::invokeMethod(Application::getInstance()->getWebcam(), "setFrame",
        Q_ARG(cv::Mat, frame), Q_ARG(int, format), Q_ARG(cv::Mat, _grayDepthFrame), Q_ARG(cv::RotatedRect, faceRect));
}

#ifdef HAVE_OPENNI
static void XN_CALLBACK_TYPE newUser(UserGenerator& generator, XnUserID id, void* cookie) {
    printLog("Found user %d.\n", id);
}
static void XN_CALLBACK_TYPE lostUser(UserGenerator& generator, XnUserID id, void* cookie) {
    printLog("Lost user %d.\n", id);
}
#endif

bool FrameGrabber::init() {
    _initialized = true;

    // load our face cascade
    switchToResourcesParentIfRequired();
    if (!_faceCascade.load("resources/haarcascades/haarcascade_frontalface_alt.xml")) {
        printLog("Failed to load Haar cascade for face tracking.\n");
        return false;
    }

    // first try for a Kinect
#ifdef HAVE_OPENNI
    _xnContext.Init();
    if (_depthGenerator.Create(_xnContext) == XN_STATUS_OK && _imageGenerator.Create(_xnContext) == XN_STATUS_OK &&
            _userGenerator.Create(_xnContext) == XN_STATUS_OK &&
                _userGenerator.IsCapabilitySupported(XN_CAPABILITY_SKELETON)) {
        _depthGenerator.GetMetaData(_depthMetaData);
        _imageGenerator.SetPixelFormat(XN_PIXEL_FORMAT_RGB24);
        _imageGenerator.GetMetaData(_imageMetaData);
        
        XnCallbackHandle userCallbacks;
        _userGenerator.RegisterUserCallbacks(newUser, lostUser, 0, userCallbacks);
        
        _xnContext.StartGeneratingAll();
        return true;
    }
#endif

    // next, an ordinary webcam
    if ((_capture = cvCaptureFromCAM(-1)) == 0) {
        printLog("Failed to open webcam.\n");
        return false;
    }
    const int IDEAL_FRAME_WIDTH = 320;
    const int IDEAL_FRAME_HEIGHT = 240;
    cvSetCaptureProperty(_capture, CV_CAP_PROP_FRAME_WIDTH, IDEAL_FRAME_WIDTH);
    cvSetCaptureProperty(_capture, CV_CAP_PROP_FRAME_HEIGHT, IDEAL_FRAME_HEIGHT);
    
#ifdef __APPLE__
    configureCamera(0x5ac, 0x8510, false, 0.975, 0.5, 1.0, 0.5, true, 0.5);
#else
    cvSetCaptureProperty(_capture, CV_CAP_PROP_EXPOSURE, 0.5);
    cvSetCaptureProperty(_capture, CV_CAP_PROP_CONTRAST, 0.5);
    cvSetCaptureProperty(_capture, CV_CAP_PROP_SATURATION, 0.5);
    cvSetCaptureProperty(_capture, CV_CAP_PROP_BRIGHTNESS, 0.5);
    cvSetCaptureProperty(_capture, CV_CAP_PROP_HUE, 0.5);
    cvSetCaptureProperty(_capture, CV_CAP_PROP_GAIN, 0.5);
#endif

    return true;
}

void FrameGrabber::updateHSVFrame(const Mat& frame, int format) {
    cvtColor(frame, _hsvFrame, format == GL_RGB ? CV_RGB2HSV : CV_BGR2HSV);
    inRange(_hsvFrame, Scalar(0, 55, 65), Scalar(180, 256, 256), _mask);
}
