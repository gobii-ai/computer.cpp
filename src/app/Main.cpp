#include "TrayIcon.h"

#include "computer_cpp/Platform.h"

#include <memory>
#include <wx/msgdlg.h>
#include <wx/wx.h>

class ComputerCppWxApp : public wxApp {
public:
    bool OnInit() override {
        wxApp::SetExitOnFrameDelete(false);
        trayIcon_ = std::make_unique<ComputerCpp::App::TrayIcon>();
        return true;
    }

    int OnExit() override {
        trayIcon_.reset();
        return wxApp::OnExit();
    }

private:
    std::unique_ptr<ComputerCpp::App::TrayIcon> trayIcon_;
};

wxIMPLEMENT_APP(ComputerCppWxApp);
