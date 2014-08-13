// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_context_menu_model.h"

#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/extensions/api/extension_action/extension_action_api.h"
#include "chrome/browser/extensions/context_menu_matcher.h"
#include "chrome/browser/extensions/extension_action.h"
#include "chrome/browser/extensions/extension_action_manager.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/menu_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/manifest_url_handler.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/context_menu_params.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

using content::OpenURLParams;
using content::Referrer;
using content::WebContents;
using extensions::Extension;
using extensions::MenuItem;
using extensions::MenuManager;

namespace {

// Returns true if the given |item| is of the given |type|.
bool MenuItemMatchesAction(ExtensionContextMenuModel::ActionType type,
                           const MenuItem* item) {
  if (type == ExtensionContextMenuModel::NO_ACTION)
    return false;

  const MenuItem::ContextList& contexts = item->contexts();

  if (contexts.Contains(MenuItem::ALL))
    return true;
  if (contexts.Contains(MenuItem::PAGE_ACTION) &&
      (type == ExtensionContextMenuModel::PAGE_ACTION))
    return true;
  if (contexts.Contains(MenuItem::BROWSER_ACTION) &&
      (type == ExtensionContextMenuModel::BROWSER_ACTION))
    return true;

  return false;
}

}  // namespace

ExtensionContextMenuModel::ExtensionContextMenuModel(const Extension* extension,
                                                     Browser* browser,
                                                     PopupDelegate* delegate)
    : SimpleMenuModel(this),
      extension_id_(extension->id()),
      browser_(browser),
      profile_(browser->profile()),
      delegate_(delegate),
      action_type_(NO_ACTION),
      extension_items_count_(0) {
  InitMenu(extension);

  if (profile_->GetPrefs()->GetBoolean(prefs::kExtensionsUIDeveloperMode) &&
      delegate_) {
    AddSeparator(ui::NORMAL_SEPARATOR);
    AddItemWithStringId(INSPECT_POPUP, IDS_EXTENSION_ACTION_INSPECT_POPUP);
  }
}

ExtensionContextMenuModel::ExtensionContextMenuModel(const Extension* extension,
                                                     Browser* browser)
    : SimpleMenuModel(this),
      extension_id_(extension->id()),
      browser_(browser),
      profile_(browser->profile()),
      delegate_(NULL),
      action_type_(NO_ACTION),
      extension_items_count_(0) {
  InitMenu(extension);
}

bool ExtensionContextMenuModel::IsCommandIdChecked(int command_id) const {
  if (command_id >= IDC_EXTENSIONS_CONTEXT_CUSTOM_FIRST &&
      command_id <= IDC_EXTENSIONS_CONTEXT_CUSTOM_LAST)
    return extension_items_->IsCommandIdChecked(command_id);
  return false;
}

bool ExtensionContextMenuModel::IsCommandIdEnabled(int command_id) const {
  const Extension* extension = GetExtension();
  if (!extension)
    return false;

  if (command_id >= IDC_EXTENSIONS_CONTEXT_CUSTOM_FIRST &&
      command_id <= IDC_EXTENSIONS_CONTEXT_CUSTOM_LAST) {
    return extension_items_->IsCommandIdEnabled(command_id);
  } else if (command_id == CONFIGURE) {
    return
        extensions::ManifestURL::GetOptionsPage(extension).spec().length() > 0;
  } else if (command_id == NAME) {
    // The NAME links to the Homepage URL. If the extension doesn't have a
    // homepage, we just disable this menu item.
    return extensions::ManifestURL::GetHomepageURL(extension).is_valid();
  } else if (command_id == INSPECT_POPUP) {
    WebContents* web_contents =
        browser_->tab_strip_model()->GetActiveWebContents();
    if (!web_contents)
      return false;

    return extension_action_ &&
        extension_action_->HasPopup(SessionID::IdForTab(web_contents));
  } else if (command_id == UNINSTALL) {
    // Some extension types can not be uninstalled.
    return extensions::ExtensionSystem::Get(
        profile_)->management_policy()->UserMayModifySettings(extension, NULL);
  }
  return true;
}

bool ExtensionContextMenuModel::GetAcceleratorForCommandId(
    int command_id, ui::Accelerator* accelerator) {
  return false;
}

void ExtensionContextMenuModel::ExecuteCommand(int command_id,
                                               int event_flags) {
  const Extension* extension = GetExtension();
  if (!extension)
    return;

  if (command_id >= IDC_EXTENSIONS_CONTEXT_CUSTOM_FIRST &&
      command_id <= IDC_EXTENSIONS_CONTEXT_CUSTOM_LAST) {
    WebContents* web_contents =
        browser_->tab_strip_model()->GetActiveWebContents();
    DCHECK(extension_items_);
    extension_items_->ExecuteCommand(
        command_id, web_contents, content::ContextMenuParams());
    return;
  }

  switch (command_id) {
    case NAME: {
      OpenURLParams params(extensions::ManifestURL::GetHomepageURL(extension),
                           Referrer(), NEW_FOREGROUND_TAB,
                           content::PAGE_TRANSITION_LINK, false);
      browser_->OpenURL(params);
      break;
    }
    case CONFIGURE:
      DCHECK(!extensions::ManifestURL::GetOptionsPage(extension).is_empty());
      extensions::ExtensionTabUtil::OpenOptionsPage(extension, browser_);
      break;
    case HIDE: {
      extensions::ExtensionActionAPI::SetBrowserActionVisibility(
          extensions::ExtensionPrefs::Get(profile_), extension->id(), false);
      break;
    }
    case UNINSTALL: {
      AddRef();  // Balanced in Accepted() and Canceled()
      extension_uninstall_dialog_.reset(
          extensions::ExtensionUninstallDialog::Create(
              profile_, browser_->window()->GetNativeWindow(), this));
      extension_uninstall_dialog_->ConfirmUninstall(extension);
      break;
    }
    case MANAGE: {
      chrome::ShowExtensions(browser_, extension->id());
      break;
    }
    case INSPECT_POPUP: {
      delegate_->InspectPopup();
      break;
    }
    default:
     NOTREACHED() << "Unknown option";
     break;
  }
}

void ExtensionContextMenuModel::ExtensionUninstallAccepted() {
  if (GetExtension()) {
    extensions::ExtensionSystem::Get(profile_)
        ->extension_service()
        ->UninstallExtension(extension_id_,
                             extensions::UNINSTALL_REASON_USER_INITIATED,
                             base::Bind(&base::DoNothing),
                             NULL);
  }
  Release();
}

void ExtensionContextMenuModel::ExtensionUninstallCanceled() {
  Release();
}

ExtensionContextMenuModel::~ExtensionContextMenuModel() {}

void ExtensionContextMenuModel::InitMenu(const Extension* extension) {
  DCHECK(extension);

  extensions::ExtensionActionManager* extension_action_manager =
      extensions::ExtensionActionManager::Get(profile_);
  extension_action_ = extension_action_manager->GetBrowserAction(*extension);
  if (!extension_action_) {
    extension_action_ = extension_action_manager->GetPageAction(*extension);
    if (extension_action_)
      action_type_ = PAGE_ACTION;
  } else {
    action_type_ = BROWSER_ACTION;
  }

  extension_items_.reset(new extensions::ContextMenuMatcher(
      profile_, this, this, base::Bind(MenuItemMatchesAction, action_type_)));

  std::string extension_name = extension->name();
  // Ampersands need to be escaped to avoid being treated like
  // mnemonics in the menu.
  base::ReplaceChars(extension_name, "&", "&&", &extension_name);
  AddItem(NAME, base::UTF8ToUTF16(extension_name));
  AppendExtensionItems();
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItemWithStringId(CONFIGURE, IDS_EXTENSIONS_OPTIONS_MENU_ITEM);
  AddItem(UNINSTALL, l10n_util::GetStringUTF16(IDS_EXTENSIONS_UNINSTALL));
  if (extension_action_manager->GetBrowserAction(*extension))
    AddItemWithStringId(HIDE, IDS_EXTENSIONS_HIDE_BUTTON);
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItemWithStringId(MANAGE, IDS_MANAGE_EXTENSION);
}

const Extension* ExtensionContextMenuModel::GetExtension() const {
  return extensions::ExtensionRegistry::Get(profile_)
      ->enabled_extensions()
      .GetByID(extension_id_);
}

void ExtensionContextMenuModel::AppendExtensionItems() {
  extension_items_->Clear();

  MenuManager* menu_manager = MenuManager::Get(profile_);
  if (!menu_manager ||
      !menu_manager->MenuItems(MenuItem::ExtensionKey(extension_id_)))
    return;

  AddSeparator(ui::NORMAL_SEPARATOR);

  extension_items_count_ = 0;
  extension_items_->AppendExtensionItems(MenuItem::ExtensionKey(extension_id_),
                                         base::string16(),
                                         &extension_items_count_,
                                         true);  // is_action_menu
}
