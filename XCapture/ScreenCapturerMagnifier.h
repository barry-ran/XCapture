#pragma once

#include <Windows.h>
#include <magnification.h>
#include <wincodec.h>

#include "ScreenCapturer.h"

class ScreenCapturerMagnifier : public ScreenCapturer
{
public:
    ScreenCapturerMagnifier();
    virtual ~ScreenCapturerMagnifier();

    // ScreenCapturer interface
    bool Start(Callback* callback) override;
    void Capture(RECT srcRect) override;
    void SetExcludedWindow(HWND hWindow) override;

private:
    typedef BOOL(WINAPI* MagImageScalingCallback)(HWND hwnd, 
                                                  void* srcdata, 
                                                  MAGIMAGEHEADER srcheader, 
                                                  void* destdata, 
                                                  MAGIMAGEHEADER destheader,
                                                  RECT unclipped,
                                                  RECT clipped,
                                                  HRGN dirty);

    typedef BOOL(WINAPI* MagInitializeFunc)(void);

    typedef BOOL(WINAPI* MagUninitializeFunc)(void);

    typedef BOOL(WINAPI* MagSetWindowSourceFunc)(HWND hwnd, RECT rect);

    typedef BOOL(WINAPI* MagSetWindowFilterListFunc)(HWND hwnd,
                                                     DWORD dwFilterMode,
                                                     int count,
                                                     HWND* pHWND);

    typedef BOOL(WINAPI* MagSetImageScalingCallbackFunc)(HWND hwnd, MagImageScalingCallback callback);

    static BOOL WINAPI OnMagImageScalingCallback(HWND hwnd,
                                                 void* srcdata,
                                                 MAGIMAGEHEADER srcheader,
                                                 void* destdata,
                                                 MAGIMAGEHEADER destheader,
                                                 RECT unclipped,
                                                 RECT clipped,
                                                 HRGN dirty);

    void OnCaptured(void* data, const MAGIMAGEHEADER& header);

    bool InitializeMagnifier();
    bool CaptureImage(RECT srcRect);

    bool m_bMagInitialized;

    // Callback to call when image is received from Magnification API
    Callback* m_Callback;

    // Import from Magnification lib
    HMODULE                         m_hMagLib;
    MagInitializeFunc               m_MagInitializeFunc;
    MagUninitializeFunc             m_MagUninitializeFunc;
    MagSetWindowSourceFunc          m_MagSetWindowSourceFunc;
    MagSetWindowFilterListFunc      m_MagSetWindowFilterListFunc;
    MagSetImageScalingCallbackFunc  m_MagSetImageScalingCallbackFunc;

    // Hidden host window
    HWND m_hHostWindow;

    // Magnifier control
    HWND m_hMagnifierControlWindow;

    // Window to exclude
    // (May be changed to list of excluded windows in future)
    HWND m_hExcludedWindow;

    bool m_bCaptureSucceeded;

    // Data to pass to renderer callback at the end
    BYTE* m_pData;
    BITMAPINFOHEADER m_bmif;
};
