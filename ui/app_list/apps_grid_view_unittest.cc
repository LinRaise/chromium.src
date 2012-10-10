// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/app_list/apps_grid_view.h"

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/stringprintf.h"
#include "base/timer.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/app_list/app_list_item_model.h"
#include "ui/app_list/app_list_item_view.h"
#include "ui/app_list/app_list_model.h"
#include "ui/app_list/pagination_model.h"
#include "ui/app_list/test/apps_grid_view_test_api.h"

namespace app_list {
namespace test {

namespace {

const int kIconDimension = 48;
const int kCols = 2;
const int kRows = 2;
const int kTilesPerPage = kCols * kRows;

const int kWidth = 320;
const int kHeight = 240;

class PageFlipWaiter : public PaginationModelObserver {
 public:
  PageFlipWaiter(MessageLoopForUI* ui_loop,
                 PaginationModel* model)
      : ui_loop_(ui_loop),
        model_(model),
        wait_(false),
        page_changed_(false) {
    model_->AddObserver(this);
  }

  virtual ~PageFlipWaiter() {
    model_->RemoveObserver(this);
  }

  bool Wait(int time_out_ms) {
    DCHECK(!wait_);
    wait_ = true;
    page_changed_ = false;
    wait_timer_.Stop();
    wait_timer_.Start(FROM_HERE,
                      base::TimeDelta::FromMilliseconds(time_out_ms),
                      this, &PageFlipWaiter::OnWaitTimeOut);
    ui_loop_->Run();
    wait_ = false;
    return page_changed_;
  }

 private:
  void OnWaitTimeOut() {
    ui_loop_->Quit();
  }

  // PaginationModelObserver overrides:
  virtual void TotalPagesChanged() OVERRIDE {
  }
  virtual void SelectedPageChanged(int old_selected,
                                   int new_selected) OVERRIDE {
    page_changed_ = true;
    if (wait_)
      ui_loop_->Quit();
  }
  virtual void TransitionChanged() OVERRIDE {
  }

  MessageLoopForUI* ui_loop_;
  PaginationModel* model_;
  bool wait_;
  bool page_changed_;
  base::OneShotTimer<PageFlipWaiter> wait_timer_;

  DISALLOW_COPY_AND_ASSIGN(PageFlipWaiter);
};

}  // namespace

class AppsGridViewTest : public testing::Test {
 public:
  AppsGridViewTest() {}
  virtual ~AppsGridViewTest() {}

  // testing::Test overrides:
  virtual void SetUp() OVERRIDE {
    apps_model_.reset(new AppListModel::Apps);
    pagination_model_.reset(new PaginationModel);

    apps_grid_view_.reset(new AppsGridView(NULL, pagination_model_.get()));
    apps_grid_view_->SetLayout(kIconDimension, kCols, kRows);
    apps_grid_view_->SetBoundsRect(gfx::Rect(gfx::Size(kWidth, kHeight)));
    apps_grid_view_->SetModel(apps_model_.get());

    test_api_.reset(new AppsGridViewTestApi(apps_grid_view_.get()));
  }
  virtual void TearDown() OVERRIDE {
    apps_grid_view_.reset();  // Release apps grid view before models.
  }

 protected:
  void PopulateApps(int n) {
    for (int i = 0; i < n; ++i) {
      std::string title = base::StringPrintf("Item %d", i);
      apps_model_->Add(CreateItem(title));
    }
  }

  // Get a string of all apps in |model| joined with ','.
  std::string GetModelContent() {
    std::string content;
    for (size_t i = 0; i < apps_model_->item_count(); ++i) {
      if (i > 0)
        content += ',';
      content += apps_model_->GetItemAt(i)->title();
    }
    return content;
  }

  AppListItemModel* CreateItem(const std::string& title) {
    AppListItemModel* item = new AppListItemModel;
    item->SetTitle(title);
    return item;
  }

  void HighlightItemAt(int index) {
    AppListItemModel* item = apps_model_->GetItemAt(index);
    item->SetHighlighted(true);
  }

  AppListItemView* GetItemViewAt(int index) {
    return static_cast<AppListItemView*>(
        test_api_->GetViewAtModelIndex(index));
  }

  AppListItemView* GetItemViewForPoint(const gfx::Point& point) {
    for (size_t i = 0; i < apps_model_->item_count(); ++i) {
      AppListItemView* view = GetItemViewAt(i);
      if (view->bounds().Contains(point))
        return view;
    }
    return NULL;
  }

  gfx::Rect GetItemTileRectAt(int row, int col) {
    DCHECK_GT(apps_model_->item_count(), 0u);

    gfx::Insets insets(apps_grid_view_->GetInsets());
    gfx::Rect rect(gfx::Point(insets.left(), insets.top()),
                   GetItemViewAt(0)->bounds().size());
    rect.Offset(col * rect.width(), row * rect.height());
    return rect;
  }

  // Points are in |apps_grid_view_|'s coordinates.
  void SimulateDrag(const gfx::Point& from, const gfx::Point& to) {
    AppListItemView* view = GetItemViewForPoint(from);
    DCHECK(view);

    gfx::Point translated_from = from.Subtract(view->bounds().origin());
    gfx::Point translated_to = to.Subtract(view->bounds().origin());

    ui::MouseEvent pressed_event(ui::ET_MOUSE_PRESSED,
                                 translated_from, translated_from, 0);
    apps_grid_view_->InitiateDrag(view, pressed_event);

    ui::MouseEvent drag_event(ui::ET_MOUSE_DRAGGED,
                              translated_to, translated_to, 0);
    apps_grid_view_->UpdateDrag(view, drag_event);
  }

  scoped_ptr<AppListModel::Apps> apps_model_;
  scoped_ptr<PaginationModel> pagination_model_;
  scoped_ptr<AppsGridView> apps_grid_view_;
  scoped_ptr<AppsGridViewTestApi> test_api_;

  MessageLoopForUI message_loop_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AppsGridViewTest);
};

TEST_F(AppsGridViewTest, CreatePage) {
  // Fully populates a page.
  const int kPages = 1;
  PopulateApps(kPages * kTilesPerPage);
  EXPECT_EQ(kPages, pagination_model_->total_pages());

  // Adds one more and gets a new page created.
  apps_model_->Add(CreateItem(std::string("Extra")));
  EXPECT_EQ(kPages + 1, pagination_model_->total_pages());
}

TEST_F(AppsGridViewTest, EnsureHighlightedVisible) {
  const int kPages = 3;
  PopulateApps(kPages * kTilesPerPage);
  EXPECT_EQ(kPages, pagination_model_->total_pages());
  EXPECT_EQ(0, pagination_model_->selected_page());

  // Highlight first one and last one one first page and first page should be
  // selected.
  HighlightItemAt(0);
  EXPECT_EQ(0, pagination_model_->selected_page());
  HighlightItemAt(kTilesPerPage - 1);
  EXPECT_EQ(0, pagination_model_->selected_page());

  // Highlight first one on 2nd page and 2nd page should be selected.
  HighlightItemAt(kTilesPerPage + 1);
  EXPECT_EQ(1, pagination_model_->selected_page());

  // Highlight last one in the model and last page should be selected.
  HighlightItemAt(apps_model_->item_count() - 1);
  EXPECT_EQ(kPages - 1, pagination_model_->selected_page());
}

TEST_F(AppsGridViewTest, RemoveSelectedLastApp) {
  const int kTotalItems = 2;
  const int kLastItemIndex = kTotalItems - 1;

  PopulateApps(kTotalItems);

  AppListItemView* last_view = GetItemViewAt(kLastItemIndex);
  apps_grid_view_->SetSelectedView(last_view);
  apps_model_->DeleteAt(kLastItemIndex);

  EXPECT_FALSE(apps_grid_view_->IsSelectedView(last_view));

  // No crash happens.
  AppListItemView* view = GetItemViewAt(0);
  apps_grid_view_->SetSelectedView(view);
  EXPECT_TRUE(apps_grid_view_->IsSelectedView(view));
}

TEST_F(AppsGridViewTest, MouseDrag) {
  const int kTotalItems = 4;
  PopulateApps(kTotalItems);
  EXPECT_EQ(std::string("Item 0,Item 1,Item 2,Item 3"),
            GetModelContent());

  gfx::Point from = GetItemTileRectAt(0, 0).CenterPoint();
  gfx::Point to = GetItemTileRectAt(0, 1).CenterPoint();

  // Dragging changes model order.
  SimulateDrag(from, to);
  apps_grid_view_->EndDrag(false);
  EXPECT_EQ(std::string("Item 1,Item 0,Item 2,Item 3"),
            GetModelContent());
  test_api_->LayoutToIdealBounds();

  // Canceling drag should keep existing order.
  SimulateDrag(from, to);
  apps_grid_view_->EndDrag(true);
  EXPECT_EQ(std::string("Item 1,Item 0,Item 2,Item 3"),
            GetModelContent());
  test_api_->LayoutToIdealBounds();

  // Deleting an item keeps remaining intact.
  SimulateDrag(from, to);
  apps_model_->DeleteAt(1);
  apps_grid_view_->EndDrag(false);
  EXPECT_EQ(std::string("Item 1,Item 2,Item 3"),
            GetModelContent());
  test_api_->LayoutToIdealBounds();

  // Adding a launcher item cancels the drag and respects the order.
  SimulateDrag(from, to);
  apps_model_->Add(CreateItem(std::string("Extra")));
  apps_grid_view_->EndDrag(false);
  EXPECT_EQ(std::string("Item 1,Item 2,Item 3,Extra"),
            GetModelContent());
  test_api_->LayoutToIdealBounds();
}

TEST_F(AppsGridViewTest, MouseDragFlipPage) {
  test_api_->SetPageFlipDelay(10);
  pagination_model_->SetTransitionDuration(10);

  PageFlipWaiter page_flip_waiter(&message_loop_,
                                  pagination_model_.get());

  const int kPages = 3;
  PopulateApps(kPages * kTilesPerPage);
  EXPECT_EQ(kPages, pagination_model_->total_pages());
  EXPECT_EQ(0, pagination_model_->selected_page());

  gfx::Point from = GetItemTileRectAt(0, 0).CenterPoint();
  gfx::Point to = gfx::Point(apps_grid_view_->width(),
                             apps_grid_view_->height() / 2);

  // Drag to right edge.
  SimulateDrag(from, to);

  // Page should be flipped after sometime.
  EXPECT_TRUE(page_flip_waiter.Wait(100));
  EXPECT_EQ(1, pagination_model_->selected_page());

  // Stay there and page should be flipped again.
  EXPECT_TRUE(page_flip_waiter.Wait(100));
  EXPECT_EQ(2, pagination_model_->selected_page());

  // Stay there longer and no page flip happen since we are at the last page.
  EXPECT_FALSE(page_flip_waiter.Wait(100));
  EXPECT_EQ(2, pagination_model_->selected_page());

  apps_grid_view_->EndDrag(true);

  // Now drag to the left edge and test the other direction.
  to.set_x(0);

  SimulateDrag(from, to);

  EXPECT_TRUE(page_flip_waiter.Wait(100));
  EXPECT_EQ(1, pagination_model_->selected_page());

  EXPECT_TRUE(page_flip_waiter.Wait(100));
  EXPECT_EQ(0, pagination_model_->selected_page());

  EXPECT_FALSE(page_flip_waiter.Wait(100));
  EXPECT_EQ(0, pagination_model_->selected_page());
  apps_grid_view_->EndDrag(true);
}

}  // namespace test
}  // namespace app_list
