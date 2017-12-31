//------------------------------------------------------------------------------
// <copyright file="BackgroundRemovalBasics.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

#pragma once

#include "resource.h"
#include "NuiApi.h"
#include "ImageRenderer.h"
#include <KinectBackgroundRemoval.h>
#include <NuiSensorChooser.h>
#include "NuiSensorChooserUI.h"

class CBackgroundRemovalBasics
{
    static const int        cBytesPerPixel    = 4;

    static const NUI_IMAGE_RESOLUTION cDepthResolution = NUI_IMAGE_RESOLUTION_320x240;
    
    // green screen background will also be scaled to this resolution
    static const NUI_IMAGE_RESOLUTION cColorResolution = NUI_IMAGE_RESOLUTION_640x480;

    static const int        cStatusMessageMaxLen = MAX_PATH*2;

public:
    /// <summary>
    /// Constructor
    /// </summary>
    CBackgroundRemovalBasics();

    /// <summary>
    /// Destructor
    /// </summary>
    ~CBackgroundRemovalBasics();

    /// <summary>
    /// Handles window messages, passes most to the class instance to handle
    /// </summary>
    /// <param name="hWnd">window message is for</param>
    /// <param name="uMsg">message</param>
    /// <param name="wParam">message data</param>
    /// <param name="lParam">additional message data</param>
    /// <returns>result of message processing</returns>
    static LRESULT CALLBACK MessageRouter(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    /// <summary>
    /// Handle windows messages for a class instance
    /// </summary>
    /// <param name="hWnd">window message is for</param>
    /// <param name="uMsg">message</param>
    /// <param name="wParam">message data</param>
    /// <param name="lParam">additional message data</param>
    /// <returns>result of message processing</returns>
    LRESULT CALLBACK        DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    /// <summary>
    /// Creates the main window and begins processing
    /// </summary>
    /// <param name="hInstance"></param>
    /// <param name="nCmdShow"></param>
    int                     Run(HINSTANCE hInstance, int nCmdShow);

    /// <summary>
    /// Called on Kinect device status changed. It will update the sensor chooser UI control
    /// based on the new sensor status. It may also updates the sensor instance if needed
    /// </summary>
    static void CALLBACK StatusChangeCallback(HRESULT hrStatus, const OLECHAR* instancename, const OLECHAR* uniqueDeviceName, void* pUserData);

private:
    HWND                               m_hWnd;
    BOOL                               m_bNearMode;

    // Current Kinect
    INuiSensor*                        m_pNuiSensor;

    // Direct2D
    ImageRenderer*                     m_pDrawBackgroundRemovalBasics;
    ID2D1Factory*                      m_pD2DFactory;
    
    HANDLE                             m_pDepthStreamHandle;
    HANDLE                             m_hNextDepthFrameEvent;
    HANDLE                             m_pColorStreamHandle;
    HANDLE                             m_hNextColorFrameEvent;
    HANDLE                             m_hNextSkeletonFrameEvent;
    HANDLE                             m_hNextBackgroundRemovedFrameEvent;

    BYTE*                              m_backgroundRGBX;
    BYTE*                              m_outputRGBX;

    INuiBackgroundRemovedColorStream*  m_pBackgroundRemovalStream;

    NuiSensorChooser*                  m_pSensorChooser;
    NuiSensorChooserUI*                m_pSensorChooserUI;

    UINT                               m_colorWidth;
    UINT                               m_colorHeight;
    UINT                               m_depthWidth;
    UINT                               m_depthHeight;
    DWORD                              m_trackedSkeleton;


    /// <summary>
    /// Load an image from a resource into a buffer
    /// </summary>
    /// <param name="resourceName">name of image resource to load</param>
    /// <param name="resourceType">type of resource to load</param>
    /// <param name="cOutputBuffer">size of output buffer, in bytes</param>
    /// <param name="outputBuffer">buffer that will hold the loaded image</param>
    /// <returns>S_OK on success, otherwise failure code</returns>
    HRESULT                 LoadResourceImage(PCWSTR resourceName, PCWSTR resourceType, DWORD cOutputBuffer, BYTE* outputBuffer);

    /// <summary>
    /// Main processing function
    /// </summary>
    void                    Update();

    /// <summary>
    /// Create the first connected Kinect found 
    /// </summary>
    /// <returns>S_OK on success, otherwise failure code</returns>
    HRESULT                 CreateFirstConnected();

    /// <summary>
    /// Create the stream that does removes the background and display a single player specified
    /// </summary>
    /// <returns>S_OK on success, otherwise failure code</returns>
    HRESULT                 CreateBackgroundRemovedColorStream();

    /// <summary>
    /// Handle new depth data
    /// </summary>
    /// <returns>S_OK on success, otherwise failure code</returns>
    HRESULT                 ProcessDepth();

    /// <summary>
    /// Handle new color data
    /// </summary>
    /// <returns>S_OK on success, otherwise failure code</returns>
    HRESULT                 ProcessColor();

    /// <summary>
    /// Handle new skeleton frame data
    /// </summary>
    /// <returns>S_OK on success, otherwise failure code</returns>
    HRESULT                 ProcessSkeleton();

    /// <summary>
    /// compose the background removed color image with the background image
    /// </summary>
    /// <returns>S_OK on success, otherwise failure code</returns>
    HRESULT                 ComposeImage();

	/// <summary>
    /// Use the sticky player logic to determine the player whom the background removed
	/// color stream should consider as foreground.
    /// </summary>
    /// <returns>S_OK on success, otherwise failure code</returns>
	HRESULT                 ChooseSkeleton(NUI_SKELETON_DATA* pSkeletonData);

    /// <summary>
    /// Set the status bar message
    /// </summary>
    /// <param name="szMessage">message to display</param>
    void                    SetStatusMessage(WCHAR* szMessage);

    /// <summary>
    /// Update the sensor and status based on the input changed flags
    /// </summary>
    /// <param name="changedFlags">The device change flags</param>
    void UpdateSensorAndStatus(DWORD changedFlags);

    /// <summary>
    /// Update the Nui Sensor Chooser UI control status
    /// </summary>
    void UpdateNscControlStatus();
};
