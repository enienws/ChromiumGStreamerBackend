// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/global_error/global_error_service.h"

#include <memory>

#include "base/macros.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/global_error/global_error.h"
#include "chrome/browser/ui/global_error/global_error_bubble_view_base.h"
#include "chrome/browser/ui/global_error/global_error_service_factory.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/test_utils.h"

namespace {

// An error that has a bubble view.
class BubbleViewError : public GlobalErrorWithStandardBubble {
 public:
  BubbleViewError() : bubble_view_close_count_(0) { }

  int bubble_view_close_count() { return bubble_view_close_count_; }

  bool HasMenuItem() override { return false; }
  int MenuItemCommandID() override {
    ADD_FAILURE();
    return 0;
  }
  base::string16 MenuItemLabel() override {
    ADD_FAILURE();
    return base::string16();
  }
  void ExecuteMenuItem(Browser* browser) override { ADD_FAILURE(); }

  bool HasBubbleView() override { return true; }
  base::string16 GetBubbleViewTitle() override { return base::string16(); }
  std::vector<base::string16> GetBubbleViewMessages() override {
    return std::vector<base::string16>();
  }
  base::string16 GetBubbleViewAcceptButtonLabel() override {
    return base::string16();
  }
  base::string16 GetBubbleViewCancelButtonLabel() override {
    return base::string16();
  }
  void OnBubbleViewDidClose(Browser* browser) override {
    EXPECT_TRUE(browser);
    ++bubble_view_close_count_;
  }
  void BubbleViewAcceptButtonPressed(Browser* browser) override {}
  void BubbleViewCancelButtonPressed(Browser* browser) override {}

 private:
  int bubble_view_close_count_;

  DISALLOW_COPY_AND_ASSIGN(BubbleViewError);
};

} // namespace

class GlobalErrorServiceBrowserTest : public InProcessBrowserTest {
};

// Test that showing a error with a bubble view works.
IN_PROC_BROWSER_TEST_F(GlobalErrorServiceBrowserTest, ShowBubbleView) {
  // This will be deleted by the GlobalErrorService.
  BubbleViewError* error = new BubbleViewError;

  GlobalErrorService* service =
      GlobalErrorServiceFactory::GetForProfile(browser()->profile());
  service->AddGlobalError(error);

  EXPECT_EQ(error, service->GetFirstGlobalErrorWithBubbleView());
  EXPECT_FALSE(error->HasShownBubbleView());
  EXPECT_EQ(0, error->bubble_view_close_count());

  // Creating a second browser window should show the bubble view.
  CreateBrowser(browser()->profile());
  EXPECT_EQ(NULL, service->GetFirstGlobalErrorWithBubbleView());
  EXPECT_TRUE(error->HasShownBubbleView());
  EXPECT_EQ(0, error->bubble_view_close_count());
}

// Test that GlobalErrorBubbleViewBase::CloseBubbleView correctly closes the
// bubble view.
IN_PROC_BROWSER_TEST_F(GlobalErrorServiceBrowserTest, CloseBubbleView) {
  // This will be deleted by the GlobalErrorService.
  BubbleViewError* error = new BubbleViewError;

  GlobalErrorService* service =
      GlobalErrorServiceFactory::GetForProfile(browser()->profile());
  service->AddGlobalError(error);

  EXPECT_EQ(error, service->GetFirstGlobalErrorWithBubbleView());
  EXPECT_FALSE(error->HasShownBubbleView());
  EXPECT_EQ(0, error->bubble_view_close_count());

  // Creating a second browser window should show the bubble view.
  CreateBrowser(browser()->profile());
  EXPECT_EQ(NULL, service->GetFirstGlobalErrorWithBubbleView());
  EXPECT_TRUE(error->HasShownBubbleView());
  EXPECT_EQ(0, error->bubble_view_close_count());

  // Explicitly close the bubble view.
  EXPECT_TRUE(error->GetBubbleView());
  error->GetBubbleView()->CloseBubbleView();
  content::RunAllPendingInMessageLoop();
  EXPECT_EQ(1, error->bubble_view_close_count());
}

// Test that bubble is silently dismissed if it is showing when the GlobalError
// instance is removed from the profile.
#if defined(OS_WIN) || defined(OS_LINUX)
// http://crbug.com/396473
#define MAYBE_BubbleViewDismissedOnRemove DISABLED_BubbleViewDismissedOnRemove
#else
#define MAYBE_BubbleViewDismissedOnRemove BubbleViewDismissedOnRemove
#endif
IN_PROC_BROWSER_TEST_F(GlobalErrorServiceBrowserTest,
                       MAYBE_BubbleViewDismissedOnRemove) {
  std::unique_ptr<BubbleViewError> error(new BubbleViewError);

  GlobalErrorService* service =
      GlobalErrorServiceFactory::GetForProfile(browser()->profile());
  service->AddGlobalError(error.get());

  EXPECT_EQ(error.get(), service->GetFirstGlobalErrorWithBubbleView());
  error->ShowBubbleView(browser());
  content::RunAllPendingInMessageLoop();
  EXPECT_TRUE(error->HasShownBubbleView());
  EXPECT_EQ(0, error->bubble_view_close_count());

  // Removing |error| from profile should dismiss the bubble view without
  // calling |error->BubbleViewDidClose|.
  service->RemoveGlobalError(error.get());
  content::RunAllPendingInMessageLoop();
  EXPECT_EQ(1, error->bubble_view_close_count());
  // |error| is no longer owned by service and will be deleted.
}
