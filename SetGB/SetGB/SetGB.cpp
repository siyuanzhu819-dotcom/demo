// SetGB

#include <uf.h>
#include <uf_defs.h>
#include <uf_exit.h>

#include <NXOpen/Callback.hxx>
#include <NXOpen/Drawings_DrawingSheetCollection.hxx>
#include <NXOpen/NXException.hxx>
#include <NXOpen/NXMessageBox.hxx>
#include <NXOpen/Part.hxx>
#include <NXOpen/PartCollection.hxx>
#include <NXOpen/Preferences_DraftingPreferenceManager.hxx>
#include <NXOpen/Preferences_LoadDraftingStandardBuilder.hxx>
#include <NXOpen/Preferences_PartPreferences.hxx>
#include <NXOpen/Session.hxx>
#include <NXOpen/UI.hxx>

#include <exception>
#include <map>
#include <sstream>
#include <string>

using namespace NXOpen;

namespace
{
    bool g_isInstalled = false;
    bool g_isApplyingStandard = false;
    int g_partModifiedHandlerId = 0;
    int g_workPartChangedHandlerId = 0;
    UF_registered_fn_p_t g_ufPartModifiedCallback = NULL;
    UF_registered_fn_p_t g_ufWorkPartChangedCallback = NULL;
    std::map<tag_t, int> g_sheetCountByPart;
    tag_t g_lastAppliedPartTag = NULL_TAG;
    int g_lastAppliedSheetCount = -1;
    
    int CountDrawingSheets(Part *part)
    {
        if (part == NULL)
        {
            return 0;
        }

        int count = 0;
        for (Drawings::DrawingSheetCollection::iterator it = part->DrawingSheets()->begin();
             it != part->DrawingSheets()->end();
             ++it)
        {
            ++count;
        }

        return count;
    }

    void ShowInfoMessage(const std::string &message)
    {
        UI::GetUI()->NXMessageBox()->Show(
            "SetGB",
            NXMessageBox::DialogTypeInformation,
            message.c_str());
    }

    void ShowErrorMessage(const std::string &message)
    {
        UI::GetUI()->NXMessageBox()->Show(
            "SetGB Startup Error",
            NXMessageBox::DialogTypeError,
            message.c_str());
    }

    void RememberSheetCount(Part *part)
    {
        if (part == NULL)
        {
            return;
        }
        g_sheetCountByPart[part->Tag()] = CountDrawingSheets(part);
    }

    void ResetApplyGuardIfNeeded(tag_t partTag, int currentCount, int previousCount)
    {
        if (partTag == g_lastAppliedPartTag && currentCount <= previousCount)
        {
            g_lastAppliedPartTag = NULL_TAG;
            g_lastAppliedSheetCount = -1;
        }
    }

    void LoadGbDraftingStandard(Part *part)
    {
        if (part == NULL)
        {
            return;
        }

        Session *theSession = Session::GetSession();
        Session::UndoMarkId markId = theSession->SetUndoMark(
            Session::MarkVisibilityInvisible,
            NXString("Load GB Drafting Standard", NXString::UTF8));

        Preferences::LoadDraftingStandardBuilder *builder =
            part->Preferences()->DraftingPreference()->CreateLoadDraftingStandardBuilder();

        try
        {
            builder->SetWelcomeMode(false);
            builder->SetLevel(Preferences::LoadDraftingStandardBuilder::LoadLevelUser);
            builder->SetName("GB");
            builder->Commit();
            theSession->SetUndoMarkName(markId, NXString("Load GB Drafting Standard", NXString::UTF8));
        }
        catch (...)
        {
            builder->Destroy();
            throw;
        }

        builder->Destroy();
    }

    void TryApplyGbForWorkPart(Part *part)
    {
        if (part == NULL || g_isApplyingStandard)
        {
            return;
        }

        const int currentCount = CountDrawingSheets(part);
        const tag_t partTag = part->Tag();
        int previousCount = 0;

        std::map<tag_t, int>::iterator found = g_sheetCountByPart.find(partTag);
        if (found != g_sheetCountByPart.end())
        {
            previousCount = found->second;
        }

        ResetApplyGuardIfNeeded(partTag, currentCount, previousCount);

        if (currentCount > previousCount)
        {
            if (g_lastAppliedPartTag == partTag && g_lastAppliedSheetCount == currentCount)
            {
                g_sheetCountByPart[partTag] = currentCount;
                return;
            }

            g_isApplyingStandard = true;
            try
            {
                LoadGbDraftingStandard(part);
                ShowInfoMessage("国标设置完成");
                g_lastAppliedPartTag = partTag;
                g_lastAppliedSheetCount = currentCount;
            }
            catch (...)
            {
                g_sheetCountByPart[partTag] = CountDrawingSheets(part);
                g_isApplyingStandard = false;
                throw;
            }
            g_isApplyingStandard = false;
        }

        g_sheetCountByPart[partTag] = CountDrawingSheets(part);
    }

    void OnPartModified(BasePart *basePart)
    {
        Part *part = dynamic_cast<Part *>(basePart);
        if (part == NULL)
        {
            return;
        }

        Session *theSession = Session::GetSession();
        Part *workPart = theSession->Parts()->Work();
        if (workPart == NULL || workPart->Tag() != part->Tag())
        {
            return;
        }

        TryApplyGbForWorkPart(workPart);
    }

    void OnWorkPartChanged(BasePart *basePart)
    {
        Session *theSession = Session::GetSession();
        Part *workPart = theSession->Parts()->Work();
        TryApplyGbForWorkPart(workPart);

        Part *oldPart = dynamic_cast<Part *>(basePart);
        RememberSheetCount(oldPart);
    }

    void UfPartCallback(UF_callback_reason_e_t reason, const void *partTagPtr, void *userData)
    {
        int ufStatus = UF_initialize();
        if (ufStatus != 0)
        {
            return;
        }

        try
        {
            Session *theSession = Session::GetSession();
            Part *workPart = theSession->Parts()->Work();
            if (reason == UF_modified_part_reason)
            {
                TryApplyGbForWorkPart(workPart);
            }
            else if (reason == UF_change_work_part_reason)
            {
                TryApplyGbForWorkPart(workPart);
            }
        }
        catch (...)
        {
        }

        UF_terminate();
    }

    void InstallCallbacks()
    {
        if (g_isInstalled)
        {
            return;
        }

        Session *theSession = Session::GetSession();
        PartCollection *parts = theSession->Parts();

        g_partModifiedHandlerId =
            parts->AddPartModifiedHandler(make_callback(OnPartModified));
        g_workPartChangedHandlerId =
            parts->AddWorkPartChangedHandler(make_callback(OnWorkPartChanged));
        UF_add_callback_function(
            UF_modified_part_reason,
            UfPartCallback,
            NULL,
            &g_ufPartModifiedCallback);
        UF_add_callback_function(
            UF_change_work_part_reason,
            UfPartCallback,
            NULL,
            &g_ufWorkPartChangedCallback);

        RememberSheetCount(parts->Work());
        g_isInstalled = true;
    }

    void ManualRun()
    {
        InstallCallbacks();

        Part *workPart = Session::GetSession()->Parts()->Work();
        if (workPart != NULL)
        {
            LoadGbDraftingStandard(workPart);
            RememberSheetCount(workPart);
            ShowInfoMessage("国标设置完成");
        }
    }
}

extern "C" DllExport void ufsta(char *param, int *retCode, int paramLen)
{
    int ufStatus = UF_initialize();
    if (ufStatus != 0)
    {
        ShowErrorMessage("UF_initialize failed in ufsta.");
        return;
    }

    try
    {
        InstallCallbacks();
    }
    catch (const NXException &e1)
    {
        ShowErrorMessage(e1.Message());
    }
    catch (const std::exception &e2)
    {
        ShowErrorMessage(e2.what());
    }
    catch (...)
    {
        ShowErrorMessage("Unknown startup exception.");
    }

    UF_terminate();
}

extern "C" DllExport void ufusr(char *param, int *retCode, int paramLen)
{
    try
    {
        ManualRun();
        ShowInfoMessage("已为新建图纸页激活 GB 自动加载监控");
    }
    catch (const NXException &e1)
    {
        UI::GetUI()->NXMessageBox()->Show("NXException", NXMessageBox::DialogTypeError, e1.Message());
    }
    catch (const std::exception &e2)
    {
        UI::GetUI()->NXMessageBox()->Show("Exception", NXMessageBox::DialogTypeError, e2.what());
    }
    catch (...)
    {
        UI::GetUI()->NXMessageBox()->Show("Exception", NXMessageBox::DialogTypeError, "Unknown Exception.");
    }
}

extern "C" DllExport int ufusr_ask_unload()
{
    return (int)Session::LibraryUnloadOptionAtTermination;
}
