#include "ConfigWizardWebViewPage.hpp"

#include "WebView.hpp"
#include "UserAccount.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "format.hpp"

#include <wx/webview.h>

namespace Slic3r { 
namespace GUI {
wxDEFINE_EVENT(EVT_LOGIN_VIA_WIZARD, Event<std::string>);

ConfigWizardWebViewPage::ConfigWizardWebViewPage(ConfigWizard *parent)
    // TRN Config wizard page headline.
    : ConfigWizardPage(parent, _L("Log into the Prusa Account (optional)"), _L("Log in (optional)"))
{
    p_user_account = wxGetApp().plater()->get_user_account();
    assert(p_user_account);
    bool logged = p_user_account->is_logged();

    // Create the webview
    m_browser_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_browser = WebView::CreateWebView(this, p_user_account->get_login_redirect_url(), {});
    if (!m_browser) {
        // TRN Config wizard page with a log in page.
        wxStaticText* fail_text = new wxStaticText(this, wxID_ANY, _L("Failed to load a web browser. Logging in is not possible in the moment."));
        append(fail_text);
        return;
    }
    if (logged) {
        // TRN Config wizard page with a log in web.
        m_text = new wxStaticText(this, wxID_ANY, format_wxstr("You are logged as %1%.", p_user_account->get_username()));       
    } else {
        // TRN Config wizard page with a log in web. first line of text.
        m_text = new wxStaticText(this, wxID_ANY, _L("Please log into your Prusa Account."));
        // TRN Config wizard page with a log in web. second line of text.
    }
    append(m_text);
    m_browser_sizer->Add(m_browser, 1, wxEXPAND);
    append(m_browser_sizer, 1, wxEXPAND);

    m_browser_sizer->Show(!logged);

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_ERROR, &ConfigWizardWebViewPage::on_error, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATING, &ConfigWizardWebViewPage::on_navigation_request, this, m_browser->GetId());

}

bool ConfigWizardWebViewPage::login_changed()
{
    assert(p_user_account && m_browser_sizer && m_text);
    bool logged = p_user_account->is_logged();
    m_browser_sizer->Show(!logged);
    if (logged) {
        // TRN Config wizard page with a log in web.
        m_text->SetLabel(format_wxstr("You are logged as %1%.", p_user_account->get_username()));
    } else {
        // TRN Config wizard page with a log in web. first line of text.
        m_text->SetLabel(_L("Please log into your Prusa Account."));
    }
    return logged;
}

void ConfigWizardWebViewPage::on_error(wxWebViewEvent &evt) 
{
#define WX_ERROR_CASE(type) \
case type: \
    category = #type; \
    break;

    wxString category;
    switch (evt.GetInt())
    {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    BOOST_LOG_TRIVIAL(error) << "WebViewPanel error: " << category;
}

void ConfigWizardWebViewPage::on_navigation_request(wxWebViewEvent &evt) 
{
    wxString url = evt.GetURL();
    if (url.starts_with(L"prusaslicer")) {
        evt.Veto();
        wxPostEvent(wxGetApp().plater(), Event<std::string>(EVT_LOGIN_VIA_WIZARD, into_u8(url)));	
    }
}

}} // namespace Slic3r::GUI