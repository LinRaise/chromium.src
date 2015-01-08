// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/toolbar/browser_actions_container.h"

#include "chrome/browser/extensions/api/extension_action/extension_action_api.h"
#include "chrome/browser/extensions/browser_action_test_util.h"
#include "chrome/browser/extensions/extension_toolbar_model.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/toolbar/browser_actions_bar_browsertest.h"
#include "chrome/browser/ui/views/extensions/browser_action_drag_data.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_action_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/common/extension.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view.h"

// TODO(devlin): Continue moving any tests that should be platform independent
// from this file to the crossplatform tests in
// chrome/browser/ui/toolbar/browser_actions_bar_browsertest.cc.

// Test that dragging browser actions works, and that dragging a browser action
// from the overflow menu results in it "popping" out (growing the container
// size by 1), rather than just reordering the extensions.
IN_PROC_BROWSER_TEST_F(BrowserActionsBarBrowserTest, DragBrowserActions) {
  LoadExtensions();

  // Sanity check: All extensions showing; order is A B C.
  EXPECT_EQ(3, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(3, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(extension_a()->id(), browser_actions_bar()->GetExtensionId(0));
  EXPECT_EQ(extension_b()->id(), browser_actions_bar()->GetExtensionId(1));
  EXPECT_EQ(extension_c()->id(), browser_actions_bar()->GetExtensionId(2));

  BrowserActionsContainer* container =
      BrowserView::GetBrowserViewForBrowser(browser())
          ->toolbar()->browser_actions();

  // Simulate a drag and drop to the right.
  ui::OSExchangeData drop_data;
  // Drag extension A from index 0...
  BrowserActionDragData browser_action_drag_data(extension_a()->id(), 0u);
  browser_action_drag_data.Write(profile(), &drop_data);
  ToolbarActionView* view = container->GetViewForId(extension_b()->id());
  // ...to the right of extension B.
  gfx::Point location(view->x() + view->width(), view->y());
  ui::DropTargetEvent target_event(
      drop_data, location, location, ui::DragDropTypes::DRAG_MOVE);

  // Drag and drop.
  container->OnDragUpdated(target_event);
  container->OnPerformDrop(target_event);

  // The order should now be B A C, since A was dragged to the right of B.
  EXPECT_EQ(extension_b()->id(), browser_actions_bar()->GetExtensionId(0));
  EXPECT_EQ(extension_a()->id(), browser_actions_bar()->GetExtensionId(1));
  EXPECT_EQ(extension_c()->id(), browser_actions_bar()->GetExtensionId(2));

  // This order should be reflected in the underlying model.
  extensions::ExtensionToolbarModel* model =
      extensions::ExtensionToolbarModel::Get(profile());
  EXPECT_EQ(extension_b(), model->toolbar_items()[0].get());
  EXPECT_EQ(extension_a(), model->toolbar_items()[1].get());
  EXPECT_EQ(extension_c(), model->toolbar_items()[2].get());

  // Simulate a drag and drop to the left.
  ui::OSExchangeData drop_data2;
  // Drag extension A from index 1...
  BrowserActionDragData browser_action_drag_data2(extension_a()->id(), 1u);
  browser_action_drag_data2.Write(profile(), &drop_data2);
  // ...to the left of extension B (which is now at index 0).
  location = gfx::Point(view->x(), view->y());
  ui::DropTargetEvent target_event2(
      drop_data2, location, location, ui::DragDropTypes::DRAG_MOVE);

  // Drag and drop.
  container->OnDragUpdated(target_event2);
  container->OnPerformDrop(target_event2);

  // Order should be restored to A B C.
  EXPECT_EQ(extension_a()->id(), browser_actions_bar()->GetExtensionId(0));
  EXPECT_EQ(extension_b()->id(), browser_actions_bar()->GetExtensionId(1));
  EXPECT_EQ(extension_c()->id(), browser_actions_bar()->GetExtensionId(2));

  // Shrink the size of the container so we have an overflow menu.
  model->SetVisibleIconCount(2u);
  EXPECT_EQ(2u, container->VisibleBrowserActions());
  ASSERT_TRUE(container->chevron());
  EXPECT_TRUE(container->chevron()->visible());

  // Simulate a drag and drop from the overflow menu.
  ui::OSExchangeData drop_data3;
  // Drag extension C from index 2 (in the overflow menu)...
  BrowserActionDragData browser_action_drag_data3(extension_c()->id(), 2u);
  browser_action_drag_data3.Write(profile(), &drop_data3);
  // ...to the left of extension B (which is back in index 1 on the main bar).
  location = gfx::Point(view->x(), view->y());
  ui::DropTargetEvent target_event3(
      drop_data3, location, location, ui::DragDropTypes::DRAG_MOVE);

  // Drag and drop.
  container->OnDragUpdated(target_event3);
  container->OnPerformDrop(target_event3);

  // The order should have changed *and* the container should have grown to
  // accommodate extension C. The new order should be A C B, and all three
  // extensions should be visible, with no overflow menu.
  EXPECT_EQ(extension_a()->id(), browser_actions_bar()->GetExtensionId(0));
  EXPECT_EQ(extension_c()->id(), browser_actions_bar()->GetExtensionId(1));
  EXPECT_EQ(extension_b()->id(), browser_actions_bar()->GetExtensionId(2));
  EXPECT_EQ(3u, container->VisibleBrowserActions());
  EXPECT_FALSE(container->chevron()->visible());
  EXPECT_TRUE(model->all_icons_visible());

  // TODO(devlin): Ideally, we'd also have tests for dragging from the legacy
  // overflow menu (i.e., chevron) to the main bar, but this requires either
  // having a fairly complicated interactive UI test or finding a good way to
  // mock up the BrowserActionOverflowMenuController.
}

// Test that changes performed in one container affect containers in other
// windows so that it is consistent.
IN_PROC_BROWSER_TEST_F(BrowserActionsBarBrowserTest, MultipleWindows) {
  LoadExtensions();
  BrowserActionsContainer* first =
      BrowserView::GetBrowserViewForBrowser(browser())->toolbar()->
          browser_actions();

  // Create a second browser.
  Browser* second_browser = new Browser(
      Browser::CreateParams(profile(), browser()->host_desktop_type()));
  BrowserActionsContainer* second =
      BrowserView::GetBrowserViewForBrowser(second_browser)->toolbar()->
          browser_actions();

  // Both containers should have the same order and visible actions, which
  // is right now A B C.
  EXPECT_EQ(3u, first->VisibleBrowserActions());
  EXPECT_EQ(3u, second->VisibleBrowserActions());
  EXPECT_EQ(extension_a()->id(), first->GetIdAt(0u));
  EXPECT_EQ(extension_a()->id(), second->GetIdAt(0u));
  EXPECT_EQ(extension_b()->id(), first->GetIdAt(1u));
  EXPECT_EQ(extension_b()->id(), second->GetIdAt(1u));
  EXPECT_EQ(extension_c()->id(), first->GetIdAt(2u));
  EXPECT_EQ(extension_c()->id(), second->GetIdAt(2u));

  // Simulate a drag and drop to the right.
  ui::OSExchangeData drop_data;
  // Drag extension A from index 0...
  BrowserActionDragData browser_action_drag_data(extension_a()->id(), 0u);
  browser_action_drag_data.Write(profile(), &drop_data);
  ToolbarActionView* view = first->GetViewForId(extension_b()->id());
  // ...to the right of extension B.
  gfx::Point location(view->x() + view->width(), view->y());
  ui::DropTargetEvent target_event(
      drop_data, location, location, ui::DragDropTypes::DRAG_MOVE);

  // Drag and drop.
  first->OnDragUpdated(target_event);
  first->OnPerformDrop(target_event);

  // The new order, B A C, should be reflected in *both* containers, even
  // though the drag only happened in the first one.
  EXPECT_EQ(extension_b()->id(), first->GetIdAt(0u));
  EXPECT_EQ(extension_b()->id(), second->GetIdAt(0u));
  EXPECT_EQ(extension_a()->id(), first->GetIdAt(1u));
  EXPECT_EQ(extension_a()->id(), second->GetIdAt(1u));
  EXPECT_EQ(extension_c()->id(), first->GetIdAt(2u));
  EXPECT_EQ(extension_c()->id(), second->GetIdAt(2u));

  // Next, simulate a resize by shrinking the container.
  first->OnResize(1, true);
  // The first and second container should each have resized.
  EXPECT_EQ(2u, first->VisibleBrowserActions());
  EXPECT_EQ(2u, second->VisibleBrowserActions());
}

// Test that the BrowserActionsContainer responds correctly when the underlying
// model enters highlight mode, and that browser actions are undraggable in
// highlight mode. (Highlight mode itself it tested more thoroughly in the
// ExtensionToolbarModel browsertests).
IN_PROC_BROWSER_TEST_F(BrowserActionsBarBrowserTest, HighlightMode) {
  LoadExtensions();

  EXPECT_EQ(3, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(3, browser_actions_bar()->NumberOfBrowserActions());

  BrowserActionsContainer* container =
      BrowserView::GetBrowserViewForBrowser(browser())
          ->toolbar()->browser_actions();

  // Currently, dragging should be enabled.
  ToolbarActionView* action_view = container->GetToolbarActionViewAt(0);
  ASSERT_TRUE(action_view);
  gfx::Point point(action_view->x(), action_view->y());
  EXPECT_TRUE(container->CanStartDragForView(action_view, point, point));

  extensions::ExtensionToolbarModel* model =
      extensions::ExtensionToolbarModel::Get(profile());

  extensions::ExtensionIdList extension_ids;
  extension_ids.push_back(extension_a()->id());
  extension_ids.push_back(extension_b()->id());
  model->HighlightExtensions(extension_ids);

  // Only two browser actions should be visible.
  EXPECT_EQ(2, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(2, browser_actions_bar()->NumberOfBrowserActions());

  // We shouldn't be able to drag in highlight mode.
  action_view = container->GetToolbarActionViewAt(0);
  EXPECT_FALSE(container->CanStartDragForView(action_view, point, point));

  // We should go back to normal after leaving highlight mode.
  model->StopHighlighting();
  EXPECT_EQ(3, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(3, browser_actions_bar()->NumberOfBrowserActions());
  action_view = container->GetToolbarActionViewAt(0);
  EXPECT_TRUE(container->CanStartDragForView(action_view, point, point));
}

// Test the behavior of the overflow container for Extension Actions.
class BrowserActionsContainerOverflowTest
    : public BrowserActionsBarRedesignBrowserTest {
 public:
  BrowserActionsContainerOverflowTest() : main_bar_(nullptr),
                                          overflow_bar_(nullptr),
                                          model_(nullptr) {
  }
  ~BrowserActionsContainerOverflowTest() override {}

 protected:
  // Returns true if the order of the ToolbarActionViews in |main_bar_|
  // and |overflow_bar_| match.
  bool ViewOrdersMatch();

  // Returns Success if the visible count matches |expected_visible|. This means
  // that the number of visible browser actions in |main_bar_| is
  // |expected_visible| and shows the first icons, and that the overflow bar
  // shows all (and only) the remainder.
  testing::AssertionResult VerifyVisibleCount(size_t expected_visible)
      WARN_UNUSED_RESULT;

  // Accessors.
  BrowserActionsContainer* main_bar() { return main_bar_; }
  BrowserActionsContainer* overflow_bar() { return overflow_bar_; }
  extensions::ExtensionToolbarModel* model() { return model_; }

 private:
  void SetUpOnMainThread() override;
  void TearDownOnMainThread() override;

  // The main BrowserActionsContainer (owned by the browser view).
  BrowserActionsContainer* main_bar_;

  // A parent view for the overflow menu.
  scoped_ptr<views::View> overflow_parent_;

  // The overflow BrowserActionsContainer. We manufacture this so that we don't
  // have to open the wrench menu.
  // Owned by the |overflow_parent_|.
  BrowserActionsContainer* overflow_bar_;

  // The associated toolbar model.
  extensions::ExtensionToolbarModel* model_;

  DISALLOW_COPY_AND_ASSIGN(BrowserActionsContainerOverflowTest);
};

void BrowserActionsContainerOverflowTest::SetUpOnMainThread() {
  BrowserActionsBarBrowserTest::SetUpOnMainThread();
  main_bar_ = BrowserView::GetBrowserViewForBrowser(browser())
                  ->toolbar()->browser_actions();
  overflow_parent_.reset(new views::View());
  overflow_parent_->set_owned_by_client();
  overflow_bar_ = new BrowserActionsContainer(browser(), main_bar_);
  overflow_parent_->AddChildView(overflow_bar_);
  model_ = extensions::ExtensionToolbarModel::Get(profile());
}

void BrowserActionsContainerOverflowTest::TearDownOnMainThread() {
  overflow_parent_.reset();
  BrowserActionsBarBrowserTest::TearDownOnMainThread();
}

bool BrowserActionsContainerOverflowTest::ViewOrdersMatch() {
  if (main_bar_->num_toolbar_actions() !=
      overflow_bar_->num_toolbar_actions())
    return false;
  for (size_t i = 0; i < main_bar_->num_toolbar_actions(); ++i) {
    if (main_bar_->GetIdAt(i) != overflow_bar_->GetIdAt(i))
      return false;
  }
  return true;
}

testing::AssertionResult
BrowserActionsContainerOverflowTest::VerifyVisibleCount(
    size_t expected_visible) {
  // Views order should always match (as it is based directly off the model).
  if (!ViewOrdersMatch())
    return testing::AssertionFailure() << "View orders don't match";

  // Loop through and check each browser action for proper visibility (which
  // implicitly also guarantees that the proper number are visible).
  for (size_t i = 0; i < overflow_bar_->num_toolbar_actions(); ++i) {
    bool visible = i < expected_visible;
    if (main_bar_->GetToolbarActionViewAt(i)->visible() != visible) {
      return testing::AssertionFailure() << "Index " << i <<
          " has improper visibility in main: " << !visible;
    }
    if (overflow_bar_->GetToolbarActionViewAt(i)->visible() == visible) {
      return testing::AssertionFailure() << "Index " << i <<
          " has improper visibility in overflow: " << visible;
    }
  }
  return testing::AssertionSuccess();
}

// Test the basic functionality of the BrowserActionsContainer in overflow mode.
IN_PROC_BROWSER_TEST_F(BrowserActionsContainerOverflowTest,
                       TestBasicActionOverflow) {
  LoadExtensions();

  // Since the overflow bar isn't attached to a view, we have to kick it in
  // order to retrigger layout each time we change the number of icons in the
  // bar.
  overflow_bar()->Layout();

  // All actions are showing, and are in the installation order.
  EXPECT_TRUE(model()->all_icons_visible());
  EXPECT_EQ(3u, model()->visible_icon_count());
  ASSERT_EQ(3u, main_bar()->num_toolbar_actions());
  EXPECT_EQ(extension_a()->id(), main_bar()->GetIdAt(0u));
  EXPECT_EQ(extension_b()->id(), main_bar()->GetIdAt(1u));
  EXPECT_EQ(extension_c()->id(), main_bar()->GetIdAt(2u));
  EXPECT_TRUE(VerifyVisibleCount(3u));

  // Reduce the visible count to 2. Order should be unchanged (A B C), but
  // only A and B should be visible on the main bar.
  model()->SetVisibleIconCount(2u);
  overflow_bar()->Layout();  // Kick.
  EXPECT_EQ(extension_a()->id(), main_bar()->GetIdAt(0u));
  EXPECT_EQ(extension_b()->id(), main_bar()->GetIdAt(1u));
  EXPECT_EQ(extension_c()->id(), main_bar()->GetIdAt(2u));
  EXPECT_TRUE(VerifyVisibleCount(2u));

  // Move extension C to the first position. Order should now be C A B, with
  // C and A visible in the main bar.
  model()->MoveExtensionIcon(extension_c()->id(), 0);
  overflow_bar()->Layout();  // Kick.
  EXPECT_EQ(extension_c()->id(), main_bar()->GetIdAt(0u));
  EXPECT_EQ(extension_a()->id(), main_bar()->GetIdAt(1u));
  EXPECT_EQ(extension_b()->id(), main_bar()->GetIdAt(2u));
  EXPECT_TRUE(VerifyVisibleCount(2u));

  // Hide action A. This results in it being sent to overflow, and reducing the
  // visible size to 1, so the order should be C A B, with only C visible in the
  // main bar.
  extensions::ExtensionActionAPI::SetBrowserActionVisibility(
      extensions::ExtensionPrefs::Get(profile()),
      extension_a()->id(),
      false);
  overflow_bar()->Layout();  // Kick.
  EXPECT_EQ(extension_c()->id(), main_bar()->GetIdAt(0u));
  EXPECT_EQ(extension_a()->id(), main_bar()->GetIdAt(1u));
  EXPECT_EQ(extension_b()->id(), main_bar()->GetIdAt(2u));
  EXPECT_TRUE(VerifyVisibleCount(1u));
}

// Test drag and drop between the overflow container and the main container.
IN_PROC_BROWSER_TEST_F(BrowserActionsContainerOverflowTest,
                       TestOverflowDragging) {
  LoadExtensions();

  // Start with one extension in overflow.
  model()->SetVisibleIconCount(2u);
  overflow_bar()->Layout();

  // Verify starting state is A B [C].
  ASSERT_EQ(3u, main_bar()->num_toolbar_actions());
  EXPECT_EQ(extension_a()->id(), main_bar()->GetIdAt(0u));
  EXPECT_EQ(extension_b()->id(), main_bar()->GetIdAt(1u));
  EXPECT_EQ(extension_c()->id(), main_bar()->GetIdAt(2u));
  EXPECT_TRUE(VerifyVisibleCount(2u));

  // Drag extension A (on the main bar) to the left of extension C (in
  // overflow).
  ui::OSExchangeData drop_data;
  BrowserActionDragData browser_action_drag_data(extension_a()->id(), 0u);
  browser_action_drag_data.Write(profile(), &drop_data);
  ToolbarActionView* view = overflow_bar()->GetViewForId(extension_c()->id());
  gfx::Point location(view->x(), view->y());
  ui::DropTargetEvent target_event(
      drop_data, location, location, ui::DragDropTypes::DRAG_MOVE);

  overflow_bar()->OnDragUpdated(target_event);
  overflow_bar()->OnPerformDrop(target_event);
  overflow_bar()->Layout();

  // Order should now be B [A C].
  EXPECT_EQ(extension_b()->id(), main_bar()->GetIdAt(0u));
  EXPECT_EQ(extension_a()->id(), main_bar()->GetIdAt(1u));
  EXPECT_EQ(extension_c()->id(), main_bar()->GetIdAt(2u));
  EXPECT_TRUE(VerifyVisibleCount(1u));

  // Drag extension A back from overflow to the main bar.
  ui::OSExchangeData drop_data2;
  BrowserActionDragData browser_action_drag_data2(extension_a()->id(), 1u);
  browser_action_drag_data2.Write(profile(), &drop_data2);
  view = main_bar()->GetViewForId(extension_b()->id());
  location = gfx::Point(view->x(), view->y());
  ui::DropTargetEvent target_event2(
      drop_data2, location, location, ui::DragDropTypes::DRAG_MOVE);

  main_bar()->OnDragUpdated(target_event2);
  main_bar()->OnPerformDrop(target_event2);

  // Order should be A B [C] again.
  EXPECT_EQ(extension_a()->id(), main_bar()->GetIdAt(0u));
  EXPECT_EQ(extension_b()->id(), main_bar()->GetIdAt(1u));
  EXPECT_EQ(extension_c()->id(), main_bar()->GetIdAt(2u));
  EXPECT_TRUE(VerifyVisibleCount(2u));

  // Drag extension C from overflow to the main bar (before extension B).
  ui::OSExchangeData drop_data3;
  BrowserActionDragData browser_action_drag_data3(extension_c()->id(), 2u);
  browser_action_drag_data3.Write(profile(), &drop_data3);
  location = gfx::Point(view->x(), view->y());
  ui::DropTargetEvent target_event3(
      drop_data3, location, location, ui::DragDropTypes::DRAG_MOVE);

  main_bar()->OnDragUpdated(target_event3);
  main_bar()->OnPerformDrop(target_event3);

  // Order should be A C B, and there should be no extensions in overflow.
  EXPECT_EQ(extension_a()->id(), main_bar()->GetIdAt(0u));
  EXPECT_EQ(extension_c()->id(), main_bar()->GetIdAt(1u));
  EXPECT_EQ(extension_b()->id(), main_bar()->GetIdAt(2u));
  EXPECT_TRUE(VerifyVisibleCount(3u));
}
