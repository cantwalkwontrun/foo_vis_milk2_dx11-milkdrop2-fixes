/* Language: en-GB. SPDX-License-Identifier: MPL-2.0 */
#include "pch.h"
#include <foo_vis_milk2/selectiontheme.h>
#include <foo_vis_milk2/resource.h>
#include <CppUnitTest.h>
#include <filesystem>
#include <fstream>
#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace MilkDrop2
{
namespace
{
struct Resources
{
    std::filesystem::path root;
    HMODULE module = nullptr;
    Resources()
    {
        wchar_t path[32768]{};
        GetModuleFileNameW(GetModuleHandleW(L"test.dll"),path,static_cast<DWORD>(std::size(path)));
        const auto bin=std::filesystem::path(path).parent_path();
        root=bin.parent_path().parent_path().parent_path();
        module=LoadLibraryExW((bin/L"foo_vis_milk2.dll").c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        Assert::IsNotNull(module);
    }
    ~Resources(){if(module)FreeLibrary(module);}
};

INT_PTR CALLBACK DummyDialog(HWND,UINT,WPARAM,LPARAM){return FALSE;}

struct HiddenDialog
{
    HWND owner = nullptr, window = nullptr;
    HiddenDialog(HMODULE module,LPCWSTR name)
    {
        owner=CreateWindowExW(0,L"STATIC",L"MilkDrop hidden selection test",WS_POPUP,0,0,0,0,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Assert::IsNotNull(owner);
        HRSRC resource=FindResourceW(module,name,RT_DIALOG);
        Assert::IsNotNull(resource);
        auto* data=static_cast<const BYTE*>(LockResource(LoadResource(module,resource)));
        std::vector<BYTE> copy(data,data+SizeofResource(module,resource));
        WORD signature=0;
        memcpy(&signature,copy.data()+2,sizeof(signature));
        const size_t styleOffset=signature==0xffff ? 12 : 0;
        DWORD style=0;
        memcpy(&style,copy.data()+styleOffset,sizeof(style));
        style &= ~(WS_VISIBLE|DS_SETFOREGROUND);
        memcpy(copy.data()+styleOffset,&style,sizeof(style));
        window=CreateDialogIndirectParamW(module,reinterpret_cast<const DLGTEMPLATE*>(copy.data()),owner,DummyDialog,0);
        Assert::IsNotNull(window);
        Assert::IsFalse(IsWindowVisible(window)!=FALSE);
    }
    ~HiddenDialog(){if(window)DestroyWindow(window);if(owner)DestroyWindow(owner);}
};

struct Canvas
{
    static constexpr int width=440;
    int height;
    HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=nullptr;
    HGDIOBJ previous=nullptr;
    void* pixels=nullptr;
    explicit Canvas(int requestedHeight=36) : height(requestedHeight)
    {
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        Assert::IsNotNull(bitmap);previous=SelectObject(dc,bitmap);
    }
    ~Canvas(){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);}
    void Save(const std::filesystem::path& path)
    {
        GdiFlush();
        BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
        file.bfSize=file.bfOffBits+width*height*4;
        BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=width;info.biHeight=-height;
        info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
        std::ofstream output(path,std::ios::binary);
        output.write(reinterpret_cast<char*>(&file),sizeof(file));
        output.write(reinterpret_cast<char*>(&info),sizeof(info));
        output.write(static_cast<const char*>(pixels),width*height*4);
        Assert::IsTrue(output.good());
    }
};

bool HighContrast()
{
    HIGHCONTRASTW settings{sizeof(settings)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(settings),&settings,0) && (settings.dwFlags&HCF_HIGHCONTRASTON);
}

void CheckDraw(HWND parent,HWND control,UINT type,bool dark,UINT state,UINT index=0)
{
    const int actualHeight=static_cast<int>(SendMessage(control,type==ODT_COMBOBOX?CB_GETITEMHEIGHT:LB_GETITEMHEIGHT,
        (state&ODS_COMBOBOXEDIT)?WPARAM(-1):0,0));
    Assert::IsTrue(actualHeight>0 && actualHeight<4096);
    Canvas canvas(actualHeight);
    DRAWITEMSTRUCT draw{};draw.CtlType=type;draw.CtlID=GetDlgCtrlID(control);draw.itemID=index;
    draw.itemAction=ODA_DRAWENTIRE;draw.itemState=state;draw.hwndItem=control;
    draw.hDC=canvas.dc;draw.rcItem={0,0,Canvas::width,canvas.height};
    SetTextColor(canvas.dc,RGB(1,2,3));SetBkColor(canvas.dc,RGB(4,5,6));SetBkMode(canvas.dc,OPAQUE);
    Assert::AreEqual(LRESULT(TRUE),SendMessage(parent,WM_DRAWITEM,draw.CtlID,reinterpret_cast<LPARAM>(&draw)));
    Assert::AreEqual(COLORREF(RGB(1,2,3)),GetTextColor(canvas.dc));
    Assert::AreEqual(COLORREF(RGB(4,5,6)),GetBkColor(canvas.dc));
    Assert::AreEqual(OPAQUE,GetBkMode(canvas.dc));
    const auto colours=MilkDropSelection::GetColours(dark,(state&ODS_SELECTED)!=0,(state&ODS_FOCUS)!=0,
        (state&(ODS_DISABLED|ODS_GRAYED))!=0,HighContrast());
    Assert::AreEqual(colours.background,GetPixel(canvas.dc,Canvas::width-8,actualHeight/2));
    if(index!=UINT(-1))
    {
        unsigned ink=0;
        for(int y=2;y<actualHeight-2;++y)for(int x=5;x<180;++x)
            ink+=GetPixel(canvas.dc,x,y)!=colours.background;
        Assert::IsTrue(ink>20);
    }
}

double Luminance(COLORREF colour)
{
    double sum=0;const double weights[]{.2126,.7152,.0722};
    const unsigned channels[]{GetRValue(colour),GetGValue(colour),GetBValue(colour)};
    for(int i=0;i<3;++i){double v=channels[i]/255.0;sum+=weights[i]*(v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4));}
    return sum;
}

struct Coverage{HMODULE module;unsigned selectors=0;};
BOOL CALLBACK CheckDialog(HMODULE module,LPCWSTR,LPWSTR name,LONG_PTR argument)
{
    auto& coverage=*reinterpret_cast<Coverage*>(argument);
    HiddenDialog dialog(module,name);
    DarkMode::CHooks theme(false);theme.AddDialogWithControls(dialog.window);
    MilkDropSelection::Hooks painting(theme);painting.AddDialog(dialog.window);
    for(HWND child=GetWindow(dialog.window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
    {
        wchar_t className[32]{};GetClassNameW(child,className,32);
        const bool combo=_wcsicmp(className,L"ComboBox")==0;
        if(!combo && _wcsicmp(className,L"ListBox")!=0) continue;
        const UINT type=combo?ODT_COMBOBOX:ODT_LISTBOX;
        Assert::IsTrue(MilkDropSelection::Hooks::IsSelector(child,type));
        HDC measure=GetDC(child);
        const auto font=reinterpret_cast<HFONT>(SendMessage(child,WM_GETFONT,0,0));
        const auto previous=SelectObject(measure,font?font:GetStockObject(DEFAULT_GUI_FONT));
        TEXTMETRICW metrics{};Assert::IsTrue(GetTextMetricsW(measure,&metrics)!=FALSE);
        SelectObject(measure,previous);ReleaseDC(child,measure);
        Assert::IsTrue(SendMessage(child,combo?CB_GETITEMHEIGHT:LB_GETITEMHEIGHT,0,0)>=metrics.tmHeight);
        if(combo) Assert::IsTrue(SendMessage(child,CB_GETITEMHEIGHT,WPARAM(-1),0)>=metrics.tmHeight);
        for(const wchar_t* text:{L"First & preset.milk",L"Second - \u03b1\u03b2.milk",L"Third preset.milk"})
            Assert::IsTrue(SendMessage(child,combo?CB_ADDSTRING:LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text))>=0);
        if(combo)
        {
            SendMessage(child,CB_SETITEMDATA,1,0x2468);SendMessage(child,CB_SETCURSEL,1,0);
        }
        else {SendMessage(child,LB_SETSEL,TRUE,0);SendMessage(child,LB_SETSEL,TRUE,2);}
        for(bool dark:{false,true})
        {
            theme.SetDark(dark);
            for(UINT state:{UINT(0),UINT(ODS_SELECTED),UINT(ODS_SELECTED|ODS_FOCUS),UINT(ODS_DISABLED),
                UINT(ODS_SELECTED|ODS_DISABLED),UINT(ODS_SELECTED|ODS_FOCUS|ODS_NOFOCUSRECT)})
                CheckDraw(dialog.window,child,type,dark,state);
            CheckDraw(dialog.window,child,type,dark,ODS_SELECTED,UINT(-1));
            if(combo) CheckDraw(dialog.window,child,type,dark,ODS_SELECTED|ODS_FOCUS|ODS_COMBOBOXEDIT);
        }
        if(combo)
        {
            Assert::AreEqual(LRESULT(1),SendMessage(child,CB_GETCURSEL,0,0));
            Assert::AreEqual(LRESULT(0x2468),SendMessage(child,CB_GETITEMDATA,1,0));
            wchar_t text[80]{};SendMessage(child,CB_GETLBTEXT,1,reinterpret_cast<LPARAM>(text));
            Assert::AreEqual(L"Second - \u03b1\u03b2.milk",text);
        }
        else Assert::AreEqual(LRESULT(2),SendMessage(child,LB_GETSELCOUNT,0,0));
        ++coverage.selectors;
    }
    return TRUE;
}
}

TEST_CLASS(SelectionThemeTest)
{
  public:
    TEST_METHOD(LongImportedEntriesAreRetrievedWithoutTruncationOrFixedBuffers)
    {
        Resources resources;HiddenDialog dialog(resources.module,MAKEINTRESOURCEW(IDD_PRESET_BLACKLIST));
        HWND list=GetDlgItem(dialog.window,IDC_BLACKLIST_LIST);
        const std::wstring imported=std::wstring(1018,L'\u03b1')+L".milk";
        Assert::AreEqual(LRESULT(0),SendMessage(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(imported.c_str())));
        std::wstring actual;
        Assert::IsTrue(MilkDropSelection::GetItemText(list,ODT_LISTBOX,0,actual));
        Assert::IsTrue(actual==imported);
        Assert::IsFalse(MilkDropSelection::GetItemText(list,ODT_LISTBOX,1,actual));
        Assert::IsTrue(actual.empty());
        Assert::IsFalse(MilkDropSelection::GetItemText(list,ODT_LISTBOX,-1,actual));
        Assert::IsFalse(MilkDropSelection::GetItemText(nullptr,ODT_LISTBOX,0,actual));
    }

    TEST_METHOD(AttachedSelectorsResizeAfterFontChangesAndDetachDuringCleanup)
    {
        Resources resources;
        for(const auto target:{std::pair<WORD,int>{IDD_PRESET_BLACKLIST,IDC_BLACKLIST_LIST},{IDD_PREFS,IDC_MAX_BYTES2}})
        {
            HiddenDialog dialog(resources.module,MAKEINTRESOURCEW(target.first));
            DarkMode::CHooks theme(true);theme.AddDialogWithControls(dialog.window);
            MilkDropSelection::Hooks painting(theme);painting.AddDialog(dialog.window);
            HWND child=GetDlgItem(dialog.window,target.second);
            const bool combo=target.second==IDC_MAX_BYTES2;
            struct FontScope
            {
                HWND child;HFONT original;HFONT large=nullptr;
                ~FontScope(){SendMessage(child,WM_SETFONT,reinterpret_cast<WPARAM>(original),FALSE);if(large)DeleteObject(large);}
            } fonts{child,reinterpret_cast<HFONT>(SendMessage(child,WM_GETFONT,0,0))};
            fonts.large=CreateFontW(-48,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            Assert::IsNotNull(fonts.large);
            const UINT get=combo?CB_GETITEMHEIGHT:LB_GETITEMHEIGHT;
            const LRESULT originalHeight=SendMessage(child,get,0,0);
            SendMessage(child,WM_SETFONT,reinterpret_cast<WPARAM>(fonts.large),FALSE);
            const LRESULT largeHeight=SendMessage(child,get,0,0);
            Assert::IsTrue(largeHeight>=48 && largeHeight>originalHeight);
            if(combo)Assert::IsTrue(SendMessage(child,CB_GETITEMHEIGHT,WPARAM(-1),0)>=48);
            SendMessage(child,combo?CB_SETITEMHEIGHT:LB_SETITEMHEIGHT,0,1);
            SendMessage(child,0x02E3,0,0); // WM_DPICHANGED_AFTERPARENT, no real monitor changes.
            Assert::AreEqual(largeHeight,SendMessage(child,get,0,0));
            painting.clear();
            SendMessage(child,combo?CB_SETITEMHEIGHT:LB_SETITEMHEIGHT,0,1);
            SendMessage(child,0x02E3,0,0);
            Assert::AreEqual(LRESULT(1),SendMessage(child,get,0,0));
        }
    }

    TEST_METHOD(DarkSelectionsHaveReadablePairsAndAccessibilityUsesSystemPairs)
    {
        for(bool focused:{false,true})for(bool disabled:{false,true})
        {
            const auto colours=MilkDropSelection::GetColours(true,true,focused,disabled,false);
            double a=Luminance(colours.text),b=Luminance(colours.background);
            Assert::IsTrue(((std::max)(a,b)+.05)/((std::min)(a,b)+.05)>=4.5);
            const auto accessible=MilkDropSelection::GetColours(true,true,focused,disabled,true);
            Assert::AreEqual(::GetSysColor(COLOR_HIGHLIGHT),accessible.background);
            Assert::AreEqual(::GetSysColor(COLOR_HIGHLIGHTTEXT),accessible.text);
        }
    }
    TEST_METHOD(AllComponentSelectorResourcesDrawReadableStatesAndPreserveSelectionData)
    {
        Resources resources;Coverage coverage{resources.module};
        Assert::IsTrue(EnumResourceNamesW(resources.module,RT_DIALOG,CheckDialog,reinterpret_cast<LONG_PTR>(&coverage))!=FALSE);
        Assert::AreEqual(27U,coverage.selectors);
        Logger::WriteMessage("All 27 selector resources passed light/dark, selected/unselected, disabled, empty, Unicode and item-data checks.\n");
    }
    TEST_METHOD(BlacklistPaintsNativeSelectionsAndCleanupDetachesTheHandler)
    {
        Resources resources;HiddenDialog dialog(resources.module,MAKEINTRESOURCEW(IDD_PRESET_BLACKLIST));
        DarkMode::CHooks theme(true);theme.AddDialogWithControls(dialog.window);
        MilkDropSelection::Hooks painting(theme);painting.AddDialog(dialog.window);
        HWND list=GetDlgItem(dialog.window,IDC_BLACKLIST_LIST);
        SendMessage(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Selected preset - readable & complete.milk"));
        const auto directory=resources.root/L"TestResults/selection-colours-20261112";
        std::filesystem::create_directories(directory);
        for(UINT state:{UINT(0),UINT(ODS_SELECTED),UINT(ODS_SELECTED|ODS_FOCUS)})
        {
            Canvas canvas;DRAWITEMSTRUCT item{ODT_LISTBOX,IDC_BLACKLIST_LIST,0,ODA_DRAWENTIRE,state,list,canvas.dc,{0,0,Canvas::width,canvas.height},0};
            Assert::AreEqual(LRESULT(TRUE),SendMessage(dialog.window,WM_DRAWITEM,IDC_BLACKLIST_LIST,reinterpret_cast<LPARAM>(&item)));
            canvas.Save(directory/(std::to_wstring(state)+L".bmp"));
        }
        painting.clear();
        Canvas canvas;DRAWITEMSTRUCT item{ODT_LISTBOX,IDC_BLACKLIST_LIST,0,ODA_DRAWENTIRE,ODS_SELECTED,list,canvas.dc,{0,0,Canvas::width,canvas.height},0};
        Assert::AreEqual(LRESULT(FALSE),SendMessage(dialog.window,WM_DRAWITEM,IDC_BLACKLIST_LIST,reinterpret_cast<LPARAM>(&item)));
    }
};
}
