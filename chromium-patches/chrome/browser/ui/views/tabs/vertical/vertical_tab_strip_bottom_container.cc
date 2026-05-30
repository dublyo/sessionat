// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_bottom_container.h"

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/sessionat/workspace_model.h"
#include "chrome/browser/sessionat/workspace_service.h"
#include "chrome/browser/sessionat/workspace_service_factory.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/new_tab_button.h"
#include "chrome/browser/ui/views/tabs/shared/new_tab_button.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_flat_edge_button.h"
#include "chrome/common/webui_url_constants.h"
#include "ui/actions/actions.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/actions/action_view_controller.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/flex_layout_view.h"
#include "ui/views/view_class_properties.h"

VerticalTabStripBottomContainer::VerticalTabStripBottomContainer(
    tabs::VerticalTabStripStateController* state_controller,
    actions::ActionItem* root_action_item,
    BrowserWindowInterface* browser,
    base::RepeatingClosure record_new_tab_button_pressed)
    : browser_(browser),
      root_action_item_(root_action_item),
      action_view_controller_(std::make_unique<views::ActionViewController>()) {
  SetProperty(views::kElementIdentifierKey,
              kVerticalTabStripBottomContainerElementId);

  auto new_tab_button = std::make_unique<shared::NewTabButton>(
      browser_,
      GetLayoutConstant(LayoutConstant::kVerticalTabStripNewTabButtonSize),
      GetLayoutConstant(LayoutConstant::kVerticalTabStripButtonIconSize));
  new_tab_button->set_context_menu_controller(this);

  new_tab_button_pressed_subscription_ =
      new_tab_button->RegisterWillInvokeActionCallback(
          record_new_tab_button_pressed);

  new_tab_button_ = AddChildView(std::move(new_tab_button));

  // Sessionat: footer summary — active workspace name + tab count, links to
  // the manager. Snapshot at construction; doesn't refresh live (workspace
  // observer wiring is a follow-up). Phase 2 will replace this with the
  // "Today: Xh Ym" focus-time widget from the spec.
  std::u16string footer_text = u"Workspaces";
  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  if (profile) {
    auto* ws_service =
        sessionat::WorkspaceServiceFactory::GetForProfile(profile);
    if (ws_service) {
      const sessionat::Workspace* active = ws_service->GetActiveWorkspace();
      if (active) {
        const size_t n = active->items.size();
        footer_text = base::UTF8ToUTF16(active->name) +
                      u"  -  " +
                      base::NumberToString16(n) +
                      (n == 1 ? u" tab" : u" tabs");
      }
    }
  }
  auto link = std::make_unique<views::Link>(footer_text);
  link->SetCallback(base::BindRepeating(
      [](BrowserWindowInterface* browser) {
        if (!browser) return;
        BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser);
        if (!view || !view->browser()) return;
        NavigateParams params(view->browser(),
                              GURL(chrome::kChromeUISessionatWorkspacesURL),
                              ui::PAGE_TRANSITION_AUTO_BOOKMARK);
        params.disposition = WindowOpenDisposition::CURRENT_TAB;
        Navigate(&params);
      },
      browser_));
  AddChildView(std::move(link));

  OnCollapseStateChanged(state_controller->GetCollapseState());
  collapsed_state_change_subscription_ =
      state_controller->RegisterOnCollapseChanged(base::BindRepeating(
          &VerticalTabStripBottomContainer::OnCollapseStateChanged,
          base::Unretained(this)));
}

VerticalTabStripBottomContainer::~VerticalTabStripBottomContainer() = default;


bool VerticalTabStripBottomContainer::IsPositionInWindowCaption(
    const gfx::Point& point) {
  for (views::View* child : children()) {
    if (!child->GetVisible()) {
      continue;
    }
    gfx::Point point_in_child = point;
    views::View::ConvertPointToTarget(this, child, &point_in_child);
    if (child->HitTestPoint(point_in_child)) {
      return false;
    }
  }
  return true;
}

void VerticalTabStripBottomContainer::OnCollapseStateChanged(
    tabs::VerticalTabStripCollapseState state) {
  // Updating the styles immediately at start of the animation by including
  // collapsing state.
  UpdateButtonStyles(state != tabs::VerticalTabStripCollapseState::kExpanded);
}

void VerticalTabStripBottomContainer::ShowContextMenuForViewImpl(
    View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (features::IsTabGroupMenuMoreEntryPointsEnabled()) {
    context_menu_model_ = std::make_unique<NewTabButtonMenuModel>(browser_);

    int32_t menu_runner_flags =
        views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU;

    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
    CHECK(browser_view);
    CHECK(browser_view->tab_strip_view());
    expand_on_hover_lock_ =
        browser_view->tab_strip_view()->GetExpandOnHoverLock(
            ExpandOnHoverLockType::kKeepCurrentState);

    // `base::Unretained(this)` is safe because `context_menu_runner_` is owned
    // by `this`, ensuring the callback cannot outlive `this`.
    auto on_menu_closed = base::BindRepeating(
        &VerticalTabStripBottomContainer::OnNewTabButtonContextMenuClosed,
        base::Unretained(this));

    context_menu_runner_ = std::make_unique<views::MenuRunner>(
        context_menu_model_.get(), menu_runner_flags,
        std::move(on_menu_closed));

    context_menu_runner_->RunMenuAt(
        source->GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
        views::MenuAnchorPosition::kTopLeft, source_type);
  }
}

void VerticalTabStripBottomContainer::OnNewTabButtonContextMenuClosed() {
  expand_on_hover_lock_.reset();
}

void VerticalTabStripBottomContainer::UpdateButtonStyles(bool collapsed) {
  auto orientation = collapsed ? views::LayoutOrientation::kVertical
                               : views::LayoutOrientation::kHorizontal;

  // Setting button's layout based on collapsed state
  SetOrientation(orientation);
  SetCrossAxisAlignment(collapsed ? views::LayoutAlignment::kStretch
                                  : views::LayoutAlignment::kStart);

  new_tab_button_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(
          orientation, views::MinimumFlexSizeRule::kScaleToMinimum,
          collapsed ? views::MaximumFlexSizeRule::kPreferred
                    : views::MaximumFlexSizeRule::kUnbounded,
          false, views::MinimumFlexSizeRule::kPreferred));

  new_tab_button_->SetInsets(GetLayoutInsets(
      collapsed ? LayoutInset::VERTICAL_TAB_STRIP_BOTTOM_BUTTON_COLLAPSED
                : LayoutInset::VERTICAL_TAB_STRIP_BOTTOM_BUTTON_UNCOLLAPSED));
}

BEGIN_METADATA(VerticalTabStripBottomContainer)
END_METADATA
