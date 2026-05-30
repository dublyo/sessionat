// Copyright 2024 Sessionat. All rights reserved.
// Workspace button for the Sessionat browser tab strip.

#include "chrome/browser/ui/views/tabs/workspace_tab_strip_button.h"

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/workspace_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/tabs/tab_strip_controller.h"
// Sessionat: Using select_window icon for workspaces
#include "components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_variant.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

// Element identifier for workspace name text field in dialog.
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kWorkspaceNameFieldId);

// Minimum width for the workspace button.
constexpr int kMinButtonWidth = 80;

// Maximum width for the workspace button.
constexpr int kMaxButtonWidth = 200;

// Default workspace name when none is set.
constexpr char16_t kDefaultWorkspaceName[] = u"Workspace";

// Menu command IDs for workspace operations.
enum WorkspaceMenuCommand {
  kCommandNewWorkspace = 100,
  kCommandManageWorkspaces = 101,
  kCommandSeparator = 102,
  // Workspace IDs start at 200
  kCommandWorkspaceBase = 200,
};

}  // namespace

WorkspaceTabStripButton::WorkspaceTabStripButton(
    TabStripController* tab_strip_controller,
    PressedCallback callback)
    : TabStripControlButton(tab_strip_controller,
                            std::move(callback),
                            vector_icons::kSelectWindowChromeRefreshIcon),
      workspace_name_(kDefaultWorkspaceName),
      controller_(tab_strip_controller) {
  // Configure button to show both icon and text.
  SetImageLabelSpacing(4);

  // Get the workspace service.
  if (controller_) {
    Browser* browser = controller_->GetBrowser();
    if (browser && browser->profile()) {
      workspace_service_ =
          sessionat::WorkspaceServiceFactory::GetForProfile(browser->profile());
      if (workspace_service_) {
        workspace_service_observation_.Observe(workspace_service_.get());

        // Set the active workspace name if available.
        const sessionat::Workspace* active =
            workspace_service_->GetActiveWorkspace();
        if (active) {
          workspace_name_ = base::UTF8ToUTF16(active->name);
        }
      }
    }
  }

  // Set initial button text.
  UpdateButtonText();

  // Set tooltip.
  SetTooltipText(u"Switch workspace");
}

WorkspaceTabStripButton::~WorkspaceTabStripButton() = default;

void WorkspaceTabStripButton::SetWorkspaceName(const std::u16string& name) {
  if (workspace_name_ != name) {
    workspace_name_ = name;
    UpdateButtonText();
  }
}

void WorkspaceTabStripButton::SetTabCount(int count) {
  if (tab_count_ != count) {
    tab_count_ = count;
    UpdateButtonText();
  }
}

void WorkspaceTabStripButton::UpdateButtonText() {
  std::u16string text;
  if (tab_count_ > 0) {
    text = workspace_name_ + u" (" + base::NumberToString16(tab_count_) + u")";
  } else {
    text = workspace_name_;
  }
  SetText(text);
}

gfx::Size WorkspaceTabStripButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = TabStripControlButton::CalculatePreferredSize(available_size);

  // Apply min/max width constraints.
  size.set_width(std::max(kMinButtonWidth, size.width()));
  size.set_width(std::min(kMaxButtonWidth, size.width()));

  return size;
}

bool WorkspaceTabStripButton::IsCommandIdChecked(int command_id) const {
  if (command_id < kCommandWorkspaceBase || !workspace_service_) {
    return false;
  }

  int workspace_index = command_id - kCommandWorkspaceBase;
  const auto& workspaces = workspace_service_->GetAllWorkspaces();
  if (workspace_index < 0 ||
      static_cast<size_t>(workspace_index) >= workspaces.size()) {
    return false;
  }

  std::string active_id = workspace_service_->GetActiveWorkspaceId();
  return workspaces[workspace_index].id == active_id;
}

bool WorkspaceTabStripButton::IsCommandIdEnabled(int command_id) const {
  return true;
}

void WorkspaceTabStripButton::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == kCommandNewWorkspace) {
    LOG(INFO) << "Sessionat: Create new workspace requested";
    ShowNewWorkspaceDialog();
  } else if (command_id == kCommandManageWorkspaces) {
    LOG(INFO) << "Sessionat: Manage workspaces requested";
    // Open the extensions management page for Sessionat.
    OpenExtensionSettings();
  } else if (command_id >= kCommandWorkspaceBase) {
    // Switch to workspace.
    int workspace_index = command_id - kCommandWorkspaceBase;
    LOG(INFO) << "Sessionat: Switch to workspace " << workspace_index;
    if (workspace_service_) {
      const auto& workspaces = workspace_service_->GetAllWorkspaces();
      if (workspace_index >= 0 &&
          static_cast<size_t>(workspace_index) < workspaces.size()) {
        const auto& workspace = workspaces[workspace_index];
        workspace_service_->SetActiveWorkspace(workspace.id);

        // Open all URLs in the workspace.
        if (!workspace.items.empty() && controller_) {
          Browser* browser = controller_->GetBrowser();
          if (browser) {
            for (const auto& item : workspace.items) {
              NavigateParams params(browser, item.url,
                                    ui::PAGE_TRANSITION_LINK);
              params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
              Navigate(&params);
            }
          }
        }
      }
    }
  }
}

void WorkspaceTabStripButton::ShowNewWorkspaceDialog() {
  if (!GetWidget() || !controller_) {
    return;
  }

  Browser* browser = controller_->GetBrowser();
  if (!browser) {
    return;
  }

  // Create a dialog model with a text field for the workspace name.
  ui::DialogModel::Builder dialog_builder;
  auto dialog_model =
      dialog_builder.SetInternalName("WorkspaceNamePrompt")
          .SetTitle(u"Create New Workspace")
          .AddOkButton(
              base::BindOnce(&WorkspaceTabStripButton::OnNewWorkspaceDialogAccepted,
                             weak_ptr_factory_.GetWeakPtr(),
                             dialog_builder.model()),
              ui::DialogModel::Button::Params().SetLabel(u"Create"))
          .AddCancelButton(base::DoNothing())
          .AddTextfield(
              kWorkspaceNameFieldId,
              u"Workspace Name", u"",
              ui::DialogModelTextfield::Params().SetAccessibleName(
                  u"Enter workspace name"))
          .SetInitiallyFocusedField(kWorkspaceNameFieldId)
          .Build();

  auto bubble = std::make_unique<views::BubbleDialogModelHost>(
      std::move(dialog_model), this, views::BubbleBorder::TOP_LEFT);
  views::BubbleDialogDelegate::CreateBubble(std::move(bubble))->Show();
}

void WorkspaceTabStripButton::OnNewWorkspaceDialogAccepted(
    ui::DialogModel* dialog_model) {
  if (!workspace_service_ || !dialog_model) {
    return;
  }

  // Get the workspace name from the text field.
  std::u16string workspace_name_u16 =
      dialog_model->GetTextfieldByUniqueId(kWorkspaceNameFieldId)->text();
  std::string workspace_name = base::UTF16ToUTF8(workspace_name_u16);

  // Use a default name if the user didn't enter anything.
  if (workspace_name.empty()) {
    workspace_name = "New Workspace";
  }

  std::string new_id = workspace_service_->CreateWorkspace(workspace_name);
  workspace_service_->SetActiveWorkspace(new_id);
}

void WorkspaceTabStripButton::OpenExtensionUI() {
  // Deprecated - use OpenExtensionSettings or OpenNewTab instead.
  OpenExtensionSettings();
}

void WorkspaceTabStripButton::OpenNewTab() {
  if (!controller_) {
    return;
  }

  Browser* browser = controller_->GetBrowser();
  if (!browser) {
    return;
  }

  // Open a new tab page.
  NavigateParams params(browser, GURL("chrome://newtab/"),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void WorkspaceTabStripButton::OpenExtensionSettings() {
  if (!controller_) {
    return;
  }

  Browser* browser = controller_->GetBrowser();
  if (!browser) {
    return;
  }

  // Open the Sessionat NTP page which contains the workspace management UI.
  NavigateParams params(browser, GURL("chrome://sessionat-newtab/"),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void WorkspaceTabStripButton::UpdateColors() {
  TabStripControlButton::UpdateColors();

  // Use toolbar text color for label.
  if (const ui::ColorProvider* cp = GetColorProvider()) {
    SetEnabledTextColors(
        ui::ColorVariant(cp->GetColor(kColorTabForegroundActiveFrameActive)));
  }
}

void WorkspaceTabStripButton::NotifyClick(const ui::Event& event) {
  TabStripControlButton::NotifyClick(event);
  // Show the workspace menu.
  ShowWorkspaceMenu();
}

void WorkspaceTabStripButton::ShowWorkspaceMenu() {
  BuildMenuModel();

  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);

  gfx::Rect bounds = GetBoundsInScreen();
  menu_runner_->RunMenuAt(
      GetWidget(), nullptr,
      gfx::Rect(bounds.x(), bounds.bottom(), bounds.width(), 0),
      views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kNone);
}

void WorkspaceTabStripButton::BuildMenuModel() {
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);

  // Add workspaces from the service.
  if (workspace_service_) {
    const auto& workspaces = workspace_service_->GetAllWorkspaces();
    int index = 0;
    for (const auto& workspace : workspaces) {
      std::u16string label = base::UTF8ToUTF16(workspace.name);
      if (!workspace.items.empty()) {
        label += u" (" + base::NumberToString16(workspace.items.size()) + u")";
      }
      menu_model_->AddCheckItem(kCommandWorkspaceBase + index, label);
      index++;
    }
  }

  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);

  // Add actions.
  menu_model_->AddItem(kCommandNewWorkspace, u"+ New Workspace");
  menu_model_->AddItem(kCommandManageWorkspaces, u"Manage Workspaces...");
}

void WorkspaceTabStripButton::OnWorkspaceCreated(
    const sessionat::Workspace& workspace) {
  // Workspace list changed, menu will be rebuilt on next open.
}

void WorkspaceTabStripButton::OnWorkspaceUpdated(
    const sessionat::Workspace& workspace) {
  // If this is the active workspace, update the button text.
  if (workspace_service_ &&
      workspace.id == workspace_service_->GetActiveWorkspaceId()) {
    workspace_name_ = base::UTF8ToUTF16(workspace.name);
    tab_count_ = static_cast<int>(workspace.items.size());
    UpdateButtonText();
  }
}

void WorkspaceTabStripButton::OnWorkspaceDeleted(
    const std::string& workspace_id) {
  // If the active workspace was deleted, update the button.
  if (workspace_service_) {
    const sessionat::Workspace* active =
        workspace_service_->GetActiveWorkspace();
    if (active) {
      workspace_name_ = base::UTF8ToUTF16(active->name);
      tab_count_ = static_cast<int>(active->items.size());
    } else {
      workspace_name_ = kDefaultWorkspaceName;
      tab_count_ = 0;
    }
    UpdateButtonText();
  }
}

void WorkspaceTabStripButton::OnActiveWorkspaceChanged(
    const std::string& workspace_id) {
  if (!workspace_service_) {
    return;
  }

  const sessionat::Workspace* workspace =
      workspace_service_->GetWorkspace(workspace_id);
  if (workspace) {
    workspace_name_ = base::UTF8ToUTF16(workspace->name);
    tab_count_ = static_cast<int>(workspace->items.size());
  } else {
    workspace_name_ = kDefaultWorkspaceName;
    tab_count_ = 0;
  }
  UpdateButtonText();
}

void WorkspaceTabStripButton::OnWorkspacesLoaded() {
  // Initial load, set the active workspace name.
  if (workspace_service_) {
    const sessionat::Workspace* active =
        workspace_service_->GetActiveWorkspace();
    if (active) {
      workspace_name_ = base::UTF8ToUTF16(active->name);
      tab_count_ = static_cast<int>(active->items.size());
      UpdateButtonText();
    }
  }
}

BEGIN_METADATA(WorkspaceTabStripButton)
END_METADATA
