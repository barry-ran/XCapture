#include "stdafx.h"
#include "ScreenCapturerMagnifier.h"

static LPCTSTR kMagnifierHostClass = L"ScreenCapturerWinMagnifierHost";
static LPCTSTR kHostWindowName = L"MagnifierHost";
static LPCTSTR kMagnifierWindowClass = L"Magnifier";
static LPCTSTR kMagnifierWindowName = L"MagnifierWindow";

DWORD ScreenCapturerMagnifier::m_dwTlsIdx = TLS_OUT_OF_INDEXES;

ScreenCapturerMagnifier::ScreenCapturerMagnifier()
    : m_Callback(NULL)
    , m_bMagInitialized(false)
    , m_hExcludedWindow(NULL)
    , m_hMagLib(NULL)
    , m_MagInitializeFunc(NULL)
    , m_MagUninitializeFunc(NULL)
    , m_MagSetWindowFilterListFunc(NULL)
    , m_MagSetImageScalingCallbackFunc(NULL)
    , m_MagSetWindowSourceFunc(NULL)
    , m_hInst(NULL)
    , m_hHostWindow(NULL)
    , m_bCaptureSucceeded(false)
    , m_pData(NULL)
{}

ScreenCapturerMagnifier::~ScreenCapturerMagnifier()
{
    if (m_hHostWindow) {
        DestroyWindow(m_hHostWindow);
    }

    if (m_bMagInitialized) {
        m_MagUninitializeFunc();
    }

    if (m_hMagLib) {
        FreeLibrary(m_hMagLib);
    }

    if (m_pData) {
        delete m_pData;
    }
}

void ScreenCapturerMagnifier::Start(Callback *callback)
{
    m_Callback = callback;

    InitializeMagnifier();
}

void ScreenCapturerMagnifier::Capture(RECT srcRect)
{
    bool result = CaptureImage(srcRect);

    if (result)
        m_Callback->OnCaptureComplete(m_pData, &m_bmif);
}

bool ScreenCapturerMagnifier::InitializeMagnifier()
{
    BOOL bResult;

    // Import necessary functions
    m_hMagLib = ::LoadLibrary(L"Magnification.dll");
    if (!m_hMagLib) {
        TRACE(L"Failed to load Magnification.dll library\n");
        return false;
    }

    m_MagInitializeFunc              = reinterpret_cast<MagInitializeFunc>(GetProcAddress(m_hMagLib, "MagInitialize"));
    m_MagUninitializeFunc            = reinterpret_cast<MagUninitializeFunc>(GetProcAddress(m_hMagLib, "MagUninitialize"));
    m_MagSetWindowSourceFunc         = reinterpret_cast<MagSetWindowSourceFunc>(GetProcAddress(m_hMagLib, "MagSetWindowSource"));
    m_MagSetWindowFilterListFunc     = reinterpret_cast<MagSetWindowFilterListFunc>(GetProcAddress(m_hMagLib, "MagSetWindowFilterList"));
    m_MagSetImageScalingCallbackFunc = reinterpret_cast<MagSetImageScalingCallbackFunc>(GetProcAddress(m_hMagLib, "MagSetImageScalingCallback"));

    if (!m_MagInitializeFunc || !m_MagUninitializeFunc || !m_MagSetWindowSourceFunc ||
        !m_MagSetWindowFilterListFunc || !m_MagSetImageScalingCallbackFunc) {
        TRACE(L"Failed to initialize one of the Magnification functions\n");
        return false;
    }

    // Initialize Magnifier
    bResult = m_MagInitializeFunc();
    if (!bResult) {
        TRACE(L"MagInitialize() failed\n");
        return false;
    }

    // Create host window
    HMODULE hInst;
    bResult = GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, 
                                 reinterpret_cast<char*>(&DefWindowProc), 
                                 &hInst);
    if (!bResult) {
        m_MagUninitializeFunc();
        TRACE(L"GetModuleHandle() failed\n");
        return false;
    }

    // Register host window class
    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = &DefWindowProc;
    wcex.hInstance = hInst;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.lpszClassName = kMagnifierHostClass;

    RegisterClassEx(&wcex);

    m_hHostWindow = CreateWindowEx(WS_EX_LAYERED,
                                   kMagnifierHostClass,
                                   kHostWindowName,
                                   0,
                                   0, 0, 0, 0,
                                   NULL,
                                   NULL,
                                   hInst,
                                   NULL);
    if (!m_hHostWindow) {
        m_MagUninitializeFunc();
        TRACE(L"Failed to create host window\n");
        return false;
    }

    // Create the magnifier control in host window created above
    m_hMagnifierControlWindow = CreateWindow(kMagnifierWindowClass,
                                             kMagnifierWindowName,
                                             WS_CHILD | WS_VISIBLE | MS_SHOWMAGNIFIEDCURSOR,
                                             0, 0, 0, 0,
                                             m_hHostWindow,
                                             NULL,
                                             m_hInst,
                                             NULL);
    if (!m_hMagnifierControlWindow) {
        m_MagUninitializeFunc();
        TRACE(L"CreteWindow for Magnifier Control failed\n");
        return false;
    }

    // Hide host window
    ShowWindow(m_hHostWindow, SW_HIDE);

    // Set callback to receive captured image
    bResult = m_MagSetImageScalingCallbackFunc(m_hMagnifierControlWindow, &ScreenCapturerMagnifier::OnMagImageScalingCallback);
    if (!bResult) {
        m_MagUninitializeFunc();
        TRACE(L"Failed to set Magnifier callback\n");
        return false;
    }

    // TODO
    // Don't forget to try to exclude window here

    if (m_dwTlsIdx == TLS_OUT_OF_INDEXES) {
        m_dwTlsIdx = TlsAlloc();
    }

    TlsSetValue(m_dwTlsIdx, this);

    m_bMagInitialized = true;
    return true;
}

BOOL ScreenCapturerMagnifier::OnMagImageScalingCallback(HWND hwnd,
                                                        void* srcdata,
                                                        MAGIMAGEHEADER srcheader,
                                                        void* destdata,
                                                        MAGIMAGEHEADER destheader,
                                                        RECT unclipped,
                                                        RECT clipped,
                                                        HRGN dirty)
{
    ScreenCapturerMagnifier* owner = reinterpret_cast<ScreenCapturerMagnifier*>(TlsGetValue(m_dwTlsIdx));

    owner->OnCaptured(srcdata, srcheader);

    return TRUE;
}

void ScreenCapturerMagnifier::OnCaptured(void* data, const MAGIMAGEHEADER& header)
{
    //TRACE(L"CAPTURED!\n");

    // Setup the bitmap info header
    m_bmif.biSize = sizeof(BITMAPINFOHEADER);
    m_bmif.biHeight = header.height;
    m_bmif.biWidth = header.width;
    m_bmif.biSizeImage = header.cbSize;
    m_bmif.biPlanes = 1;
    m_bmif.biBitCount = (WORD)(m_bmif.biSizeImage / m_bmif.biHeight / m_bmif.biWidth * 8);
    m_bmif.biCompression = BI_RGB;

    if (m_pData != NULL) {
        delete m_pData;
        m_pData = NULL;
    }

    m_pData = (BYTE*)calloc(m_bmif.biSizeImage, sizeof(BYTE));
    memcpy(m_pData, data, m_bmif.biSizeImage);

    // The data bit is in top->bottom order, so we convert it to bottom->top order
    LONG lineSize = m_bmif.biWidth * m_bmif.biBitCount / 8;
    BYTE* pLineData = new BYTE[lineSize];
    BYTE* pStart;
    BYTE* pEnd;
    LONG lineStart = 0;
    LONG lineEnd = m_bmif.biHeight - 1;
    while (lineStart < lineEnd)
    {
        // Get the address of the swap line
        pStart = m_pData + (lineStart * lineSize);
        pEnd = m_pData + (lineEnd * lineSize);
        // Swap the top with the bottom
        memcpy(pLineData, pStart, lineSize);
        memcpy(pStart, pEnd, lineSize);
        memcpy(pEnd, pLineData, lineSize);

        // Adjust the line index
        lineStart++;
        lineEnd--;
    }
    delete pLineData;

    m_bCaptureSucceeded = true;
}

bool ScreenCapturerMagnifier::CaptureImage(RECT srcRect)
{
    BOOL bResult;

    bResult = SetWindowPos(m_hMagnifierControlWindow, NULL, 
                           srcRect.left, srcRect.top, 
                           srcRect.right, srcRect.bottom, 0);
    if (!bResult) {
        TRACE(L"SetWindowPos() failed");
        return false;
    }

    m_bCaptureSucceeded = false;
    
    // !!! IMPORTANT
    // OnCaptured will be called via OnMagImageScalingCallback BEFORE
    // m_MagSetWindowSourceFunc() returns
    // So, we can safely return m_bCaptureSucceeded as a param!
    bResult = m_MagSetWindowSourceFunc(m_hMagnifierControlWindow, srcRect);
    if (!bResult) {
        TRACE(L"MagSetWindowSource() failed");
        return false;
    }

    return m_bCaptureSucceeded;
}

void ScreenCapturerMagnifier::SetExcludedWindow()
{
    return;
}