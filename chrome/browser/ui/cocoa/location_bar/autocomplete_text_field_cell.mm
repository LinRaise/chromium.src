// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/location_bar/autocomplete_text_field_cell.h"

#include "base/logging.h"
#include "base/mac/foundation_util.h"
#include "base/mac/mac_logging.h"
#import "chrome/browser/ui/cocoa/location_bar/autocomplete_text_field.h"
#import "chrome/browser/ui/cocoa/location_bar/button_decoration.h"
#import "chrome/browser/ui/cocoa/location_bar/location_bar_decoration.h"
#import "chrome/browser/ui/cocoa/nsview_additions.h"
#import "chrome/browser/ui/cocoa/tracking_area.h"
#import "chrome/common/extensions/extension_switch_utils.h"
#import "third_party/mozilla/NSPasteboard+Utils.h"

namespace {

const CGFloat kBaselineAdjust = 3.0;

// Matches the clipping radius of |GradientButtonCell|.
const CGFloat kCornerRadius = 4.0;

// How far to inset the left-hand decorations from the field's bounds.
const CGFloat kLeftDecorationXOffset = 5.0;

NSString* const kButtonDecorationKey = @"ButtonDecoration";

// How far to inset the right-hand decorations from the field's bounds.
// TODO(shess): Why is this different from |kLeftDecorationXOffset|?
// |kDecorationOuterXOffset|?
CGFloat RightDecorationXOffset() {
  const CGFloat kRightDecorationXOffset = 5.0;
  const CGFloat kScriptBadgeRightDecorationXOffset = 9.0;
  const CGFloat kActionBoxRightDecorationXOffset = 0.0;

  if (extensions::switch_utils::AreScriptBadgesEnabled()) {
    return kScriptBadgeRightDecorationXOffset;
  } else if (extensions::switch_utils::IsActionBoxEnabled()) {
    return kActionBoxRightDecorationXOffset;
  } else {
    return kRightDecorationXOffset;
  }
}

// The amount of padding on either side reserved for drawing
// decorations.  [Views has |kItemPadding| == 3.]
CGFloat DecorationHorizontalPad() {
  const CGFloat kDecorationHorizontalPad = 3.0;
  const CGFloat kScriptBadgeDecorationHorizontalPad = 9.0;

  return extensions::switch_utils::AreScriptBadgesEnabled() ?
      kScriptBadgeDecorationHorizontalPad : kDecorationHorizontalPad;
}

// How long to wait for mouse-up on the location icon before assuming
// that the user wants to drag.
const NSTimeInterval kLocationIconDragTimeout = 0.25;

// Calculate the positions for a set of decorations.  |frame| is the
// overall frame to do layout in, |remaining_frame| will get the
// left-over space.  |all_decorations| is the set of decorations to
// lay out, |decorations| will be set to the decorations which are
// visible and which fit, in the same order as |all_decorations|,
// while |decoration_frames| will be the corresponding frames.
// |x_edge| describes the edge to layout the decorations against
// (|NSMinXEdge| or |NSMaxXEdge|).  |initial_padding| is the padding
// from the edge of |cell_frame| (|DecorationHorizontalPad()| is used
// between decorations).
void CalculatePositionsHelper(
    NSRect frame,
    const std::vector<LocationBarDecoration*>& all_decorations,
    NSRectEdge x_edge,
    CGFloat initial_padding,
    std::vector<LocationBarDecoration*>* decorations,
    std::vector<NSRect>* decoration_frames,
    NSRect* remaining_frame) {
  DCHECK(x_edge == NSMinXEdge || x_edge == NSMaxXEdge);
  DCHECK_EQ(decorations->size(), decoration_frames->size());

  // The outer-most decoration will be inset a bit further from the
  // edge.
  CGFloat padding = initial_padding;

  for (size_t i = 0; i < all_decorations.size(); ++i) {
    if (all_decorations[i]->IsVisible()) {
      NSRect padding_rect, available;

      // Peel off the outside padding.
      NSDivideRect(frame, &padding_rect, &available, padding, x_edge);

      // Find out how large the decoration will be in the remaining
      // space.
      const CGFloat used_width =
          all_decorations[i]->GetWidthForSpace(NSWidth(available));

      if (used_width != LocationBarDecoration::kOmittedWidth) {
        DCHECK_GT(used_width, 0.0);
        NSRect decoration_frame;

        // Peel off the desired width, leaving the remainder in
        // |frame|.
        NSDivideRect(available, &decoration_frame, &frame,
                     used_width, x_edge);

        decorations->push_back(all_decorations[i]);
        decoration_frames->push_back(decoration_frame);
        DCHECK_EQ(decorations->size(), decoration_frames->size());

        // Adjust padding for between decorations.
        padding = DecorationHorizontalPad();
      }
    }
  }

  DCHECK_EQ(decorations->size(), decoration_frames->size());
  *remaining_frame = frame;
}

// Helper function for calculating placement of decorations w/in the
// cell.  |frame| is the cell's boundary rectangle, |remaining_frame|
// will get any space left after decorations are laid out (for text).
// |left_decorations| is a set of decorations for the left-hand side
// of the cell, |right_decorations| for the right-hand side.
// |decorations| will contain the resulting visible decorations, and
// |decoration_frames| will contain their frames in the same
// coordinates as |frame|.  Decorations will be ordered left to right.
// As a convenience returns the index of the first right-hand
// decoration.
size_t CalculatePositionsInFrame(
    NSRect frame,
    const std::vector<LocationBarDecoration*>& left_decorations,
    const std::vector<LocationBarDecoration*>& right_decorations,
    std::vector<LocationBarDecoration*>* decorations,
    std::vector<NSRect>* decoration_frames,
    NSRect* remaining_frame) {
  decorations->clear();
  decoration_frames->clear();

  // Layout |left_decorations| against the LHS.
  CalculatePositionsHelper(frame, left_decorations,
                           NSMinXEdge, kLeftDecorationXOffset,
                           decorations, decoration_frames, &frame);
  DCHECK_EQ(decorations->size(), decoration_frames->size());

  // Capture the number of visible left-hand decorations.
  const size_t left_count = decorations->size();

  // Layout |right_decorations| against the RHS.
  CalculatePositionsHelper(frame, right_decorations,
                           NSMaxXEdge, RightDecorationXOffset(),
                           decorations, decoration_frames, &frame);
  DCHECK_EQ(decorations->size(), decoration_frames->size());

  // Reverse the right-hand decorations so that overall everything is
  // sorted left to right.
  std::reverse(decorations->begin() + left_count, decorations->end());
  std::reverse(decoration_frames->begin() + left_count,
               decoration_frames->end());

  *remaining_frame = frame;
  if (extensions::switch_utils::AreScriptBadgesEnabled()) {
    // Keep the padding distance between the right-most decoration and the edit
    // box, so that any decoration background isn't overwritten by the edit
    // box's background.
    NSRect dummy;
    NSDivideRect(frame, &dummy, remaining_frame,
                 DecorationHorizontalPad(), NSMaxXEdge);
  }
  return left_count;
}

// Wrapper for |CalculatePositionsInFrame|, that snaps the edges of the leftmost
// and/or rightmost decorations' frames to the edge of the cell, if there are
// left and/or right decorations and if they are ButtonDecorations.
void CalculateSnappedPositionsInFrame(
    NSRect frame,
    const std::vector<LocationBarDecoration*>& left_decorations,
    const std::vector<LocationBarDecoration*>& right_decorations,
    std::vector<LocationBarDecoration*>* decorations,
    std::vector<NSRect>* decoration_frames,
    NSRect* remaining_frame,
    NSView* control_view) {
  // The Action Box is snapped to the right edge, so subtract the line width to
  // draw up to but excluding the omnibox's border.
  if (extensions::switch_utils::IsActionBoxEnabled())
    frame.size.width -= [control_view cr_lineWidth];

  size_t left_count = CalculatePositionsInFrame(frame,
                                                left_decorations,
                                                right_decorations,
                                                decorations,
                                                decoration_frames,
                                                remaining_frame);

  if (left_count > 0 && decorations->front()->AsButtonDecoration()) {
    NSRect& snap_frame = decoration_frames->front();
    snap_frame.size.width = NSMaxX(snap_frame) - NSMinX(frame);
    snap_frame.origin.x = NSMinX(frame);
  }
  if (left_count < decoration_frames->size() &&
      decorations->back()->AsButtonDecoration()) {
    NSRect& snap_frame = decoration_frames->back();
    snap_frame.size.width = NSMaxX(frame) - NSMinX(snap_frame);
  }
}

}  // namespace

@implementation AutocompleteTextFieldCell

- (CGFloat)baselineAdjust {
  return kBaselineAdjust;
}

- (CGFloat)cornerRadius {
  return kCornerRadius;
}

- (BOOL)shouldDrawBezel {
  return YES;
}

- (void)clearDecorations {
  leftDecorations_.clear();
  rightDecorations_.clear();
}

- (void)addLeftDecoration:(LocationBarDecoration*)decoration {
  leftDecorations_.push_back(decoration);
}

- (void)addRightDecoration:(LocationBarDecoration*)decoration {
  rightDecorations_.push_back(decoration);
}

- (CGFloat)availableWidthInFrame:(const NSRect)frame {
  std::vector<LocationBarDecoration*> decorations;
  std::vector<NSRect> decorationFrames;
  NSRect textFrame;
  CalculatePositionsInFrame(frame, leftDecorations_, rightDecorations_,
                            &decorations, &decorationFrames, &textFrame);

  return NSWidth(textFrame);
}

- (NSRect)frameForDecoration:(const LocationBarDecoration*)aDecoration
                     inFrame:(NSRect)cellFrame {
  // Short-circuit if the decoration is known to be not visible.
  if (aDecoration && !aDecoration->IsVisible())
    return NSZeroRect;

  // Layout the decorations.
  std::vector<LocationBarDecoration*> decorations;
  std::vector<NSRect> decorationFrames;
  NSRect textFrame;
  CalculatePositionsInFrame(cellFrame, leftDecorations_, rightDecorations_,
                            &decorations, &decorationFrames, &textFrame);

  // Find our decoration and return the corresponding frame.
  std::vector<LocationBarDecoration*>::const_iterator iter =
      std::find(decorations.begin(), decorations.end(), aDecoration);
  if (iter != decorations.end()) {
    const size_t index = iter - decorations.begin();
    return decorationFrames[index];
  }

  // Decorations which are not visible should have been filtered out
  // at the top, but return |NSZeroRect| rather than a 0-width rect
  // for consistency.
  NOTREACHED();
  return NSZeroRect;
}

// Overriden to account for the decorations.
- (NSRect)textFrameForFrame:(NSRect)cellFrame {
  // Get the frame adjusted for decorations.
  std::vector<LocationBarDecoration*> decorations;
  std::vector<NSRect> decorationFrames;
  NSRect textFrame = [super textFrameForFrame:cellFrame];
  CalculatePositionsInFrame(textFrame, leftDecorations_, rightDecorations_,
                            &decorations, &decorationFrames, &textFrame);

  // NOTE: This function must closely match the logic in
  // |-drawInteriorWithFrame:inView:|.

  return textFrame;
}

// Returns the sub-frame where clicks can happen within the cell.
- (NSRect)clickableFrameForFrame:(NSRect)cellFrame {
  return [super textFrameForFrame:cellFrame];
}

- (NSRect)textCursorFrameForFrame:(NSRect)cellFrame {
  std::vector<LocationBarDecoration*> decorations;
  std::vector<NSRect> decorationFrames;
  NSRect textFrame;
  size_t left_count =
      CalculatePositionsInFrame(cellFrame, leftDecorations_, rightDecorations_,
                                &decorations, &decorationFrames, &textFrame);

  // Determine the left-most extent for the i-beam cursor.
  CGFloat minX = NSMinX(textFrame);
  for (size_t index = left_count; index--; ) {
    if (decorations[index]->AcceptsMousePress())
      break;

    // If at leftmost decoration, expand to edge of cell.
    if (!index) {
      minX = NSMinX(cellFrame);
    } else {
      minX = NSMinX(decorationFrames[index]) - DecorationHorizontalPad();
    }
  }

  // Determine the right-most extent for the i-beam cursor.
  CGFloat maxX = NSMaxX(textFrame);
  for (size_t index = left_count; index < decorations.size(); ++index) {
    if (decorations[index]->AcceptsMousePress())
      break;

    // If at rightmost decoration, expand to edge of cell.
    if (index == decorations.size() - 1) {
      maxX = NSMaxX(cellFrame);
    } else {
      maxX = NSMaxX(decorationFrames[index]) + DecorationHorizontalPad();
    }
  }

  // I-beam cursor covers left-most to right-most.
  return NSMakeRect(minX, NSMinY(textFrame), maxX - minX, NSHeight(textFrame));
}

- (void)drawInteriorWithFrame:(NSRect)cellFrame inView:(NSView*)controlView {
  std::vector<LocationBarDecoration*> decorations;
  std::vector<NSRect> decorationFrames;
  NSRect workingFrame;

  CalculateSnappedPositionsInFrame(cellFrame, leftDecorations_,
                                   rightDecorations_, &decorations,
                                   &decorationFrames, &workingFrame,
                                   controlView);

  // Draw the decorations.
  for (size_t i = 0; i < decorations.size(); ++i) {
    if (decorations[i]) {
      NSRect background_frame = NSInsetRect(
          decorationFrames[i], -(DecorationHorizontalPad() + 1) / 2, 2);
      decorations[i]->DrawWithBackgroundInFrame(
          background_frame, decorationFrames[i], controlView);
    }
  }

  // NOTE: This function must closely match the logic in
  // |-textFrameForFrame:|.

  // Superclass draws text portion WRT original |cellFrame|.
  [super drawInteriorWithFrame:cellFrame inView:controlView];
}

- (LocationBarDecoration*)decorationForEvent:(NSEvent*)theEvent
                                      inRect:(NSRect)cellFrame
                                      ofView:(AutocompleteTextField*)controlView
{
  const BOOL flipped = [controlView isFlipped];
  const NSPoint location =
      [controlView convertPoint:[theEvent locationInWindow] fromView:nil];

  std::vector<LocationBarDecoration*> decorations;
  std::vector<NSRect> decorationFrames;
  NSRect textFrame;
  CalculateSnappedPositionsInFrame(cellFrame, leftDecorations_,
                                   rightDecorations_, &decorations,
                                   &decorationFrames, &textFrame, controlView);

  for (size_t i = 0; i < decorations.size(); ++i) {
    if (NSMouseInRect(location, decorationFrames[i], flipped))
      return decorations[i];
  }

  return NULL;
}

- (NSMenu*)decorationMenuForEvent:(NSEvent*)theEvent
                           inRect:(NSRect)cellFrame
                           ofView:(AutocompleteTextField*)controlView {
  LocationBarDecoration* decoration =
      [self decorationForEvent:theEvent inRect:cellFrame ofView:controlView];
  if (decoration)
    return decoration->GetMenu();
  return nil;
}

- (BOOL)mouseDown:(NSEvent*)theEvent
           inRect:(NSRect)cellFrame
           ofView:(AutocompleteTextField*)controlView {
  LocationBarDecoration* decoration =
      [self decorationForEvent:theEvent inRect:cellFrame ofView:controlView];
  if (!decoration || !decoration->AcceptsMousePress())
    return NO;

  NSRect decorationRect =
      [self frameForDecoration:decoration inFrame:cellFrame];

  // If the decoration is draggable, then initiate a drag if the user
  // drags or holds the mouse down for awhile.
  if (decoration->IsDraggable()) {
    NSDate* timeout =
        [NSDate dateWithTimeIntervalSinceNow:kLocationIconDragTimeout];
    NSEvent* event = [NSApp nextEventMatchingMask:(NSLeftMouseDraggedMask |
                                                   NSLeftMouseUpMask)
                                        untilDate:timeout
                                           inMode:NSEventTrackingRunLoopMode
                                          dequeue:YES];
    if (!event || [event type] == NSLeftMouseDragged) {
      NSPasteboard* pboard = decoration->GetDragPasteboard();
      DCHECK(pboard);

      NSImage* image = decoration->GetDragImage();
      DCHECK(image);

      NSRect dragImageRect = decoration->GetDragImageFrame(decorationRect);

      // Center under mouse horizontally, with cursor below so the image
      // can be seen.
      const NSPoint mousePoint =
          [controlView convertPoint:[theEvent locationInWindow] fromView:nil];
      dragImageRect.origin =
          NSMakePoint(mousePoint.x - NSWidth(dragImageRect) / 2.0,
                      mousePoint.y - NSHeight(dragImageRect));

      // -[NSView dragImage:at:*] wants the images lower-left point,
      // regardless of -isFlipped.  Converting the rect to window base
      // coordinates doesn't require any special-casing.  Note that
      // -[NSView dragFile:fromRect:*] takes a rect rather than a
      // point, likely for this exact reason.
      const NSPoint dragPoint =
          [controlView convertRect:dragImageRect toView:nil].origin;
      [[controlView window] dragImage:image
                                   at:dragPoint
                               offset:NSZeroSize
                                event:theEvent
                           pasteboard:pboard
                               source:self
                            slideBack:YES];

      return YES;
    }

    // On mouse-up fall through to mouse-pressed case.
    DCHECK_EQ([event type], NSLeftMouseUp);
  }

  bool handled;
  if (decoration->AsButtonDecoration()) {
    handled = decoration->AsButtonDecoration()->OnMousePressedWithView(
        decorationRect, controlView);

    // Update tracking areas and make sure the button's state is consistent with
    // the mouse's location (e.g. "normal" if the mouse is no longer over the
    // decoration, "hover" otherwise).
    [self setUpTrackingAreasInRect:cellFrame ofView:controlView];
  } else {
    handled = decoration->OnMousePressed(decorationRect);
  }

  return handled ? YES : NO;
}

// Helper method for the |mouseEntered:inView:| and |mouseExited:inView:|
// messages. Retrieves the |ButtonDecoration| for the specified event (received
// from a tracking area), and returns |NULL| if no decoration matches.
- (ButtonDecoration*)getButtonDecorationForEvent:(NSEvent*)theEvent {
  ButtonDecoration* bd = static_cast<ButtonDecoration*>(
      [[[[theEvent trackingArea] userInfo] valueForKey:kButtonDecorationKey]
          pointerValue]);

  CHECK(!bd ||
      std::count(leftDecorations_.begin(), leftDecorations_.end(), bd) ||
      std::count(rightDecorations_.begin(), rightDecorations_.end(), bd));

  return bd;
}

// Helper method for |setUpTrackingAreasInView|. Creates an |NSDictionary| to
// be used as user information to identify which decoration is the source of an
// event (from a tracking area).
- (NSDictionary*)getDictionaryForButtonDecoration:
    (ButtonDecoration*)decoration {
  if (!decoration)
    return nil;

  DCHECK(
    std::count(leftDecorations_.begin(), leftDecorations_.end(), decoration) ||
    std::count(rightDecorations_.begin(), rightDecorations_.end(), decoration));

  return [NSDictionary
      dictionaryWithObject:[NSValue valueWithPointer:decoration]
                    forKey:kButtonDecorationKey];
}

- (void)mouseEntered:(NSEvent*)theEvent
              inView:(AutocompleteTextField*)controlView {
  ButtonDecoration* decoration = [self getButtonDecorationForEvent:theEvent];
  if (decoration) {
    decoration->SetButtonState(ButtonDecoration::kButtonStateHover);
    [controlView setNeedsDisplay:YES];
  }
}

- (void)mouseExited:(NSEvent*)theEvent
             inView:(AutocompleteTextField*)controlView {
  ButtonDecoration* decoration = [self getButtonDecorationForEvent:theEvent];
  if (decoration) {
    decoration->SetButtonState(ButtonDecoration::kButtonStateNormal);
    [controlView setNeedsDisplay:YES];
  }
}

- (void)setUpTrackingAreasInRect:(NSRect)frame
                          ofView:(AutocompleteTextField*)view {
  std::vector<LocationBarDecoration*> decorations;
  std::vector<NSRect> decorationFrames;
  NSRect textFrame;
  NSRect cellRect = [self clickableFrameForFrame:[view bounds]];
  CalculateSnappedPositionsInFrame(cellRect, leftDecorations_,
                                   rightDecorations_, &decorations,
                                   &decorationFrames, &textFrame, view);

  // Remove previously-registered tracking areas, since we'll update them below.
  for (CrTrackingArea* area in [view trackingAreas]) {
    if ([[area userInfo] objectForKey:kButtonDecorationKey])
      [view removeTrackingArea:area];
  }

  // Setup new tracking areas for the buttons.
  for (size_t i = 0; i < decorations.size(); ++i) {
    ButtonDecoration* button = decorations[i]->AsButtonDecoration();
    if (button) {
      // If the button isn't pressed (in which case we want to leave it as-is),
      // update it's state since we might have missed some entered/exited events
      // because of the removing/adding of the tracking areas.
      if (button->GetButtonState() !=
          ButtonDecoration::kButtonStatePressed) {
        const NSPoint mouseLocationWindow =
            [[view window] mouseLocationOutsideOfEventStream];
        const NSPoint mouseLocation =
            [view convertPoint:mouseLocationWindow fromView:nil];
        const BOOL mouseInRect = NSMouseInRect(
            mouseLocation, decorationFrames[i], [view isFlipped]);
        button->SetButtonState(mouseInRect ?
                                   ButtonDecoration::kButtonStateHover :
                                   ButtonDecoration::kButtonStateNormal);
        [view setNeedsDisplay:YES];
      }

      NSDictionary* info = [self getDictionaryForButtonDecoration:button];
      scoped_nsobject<CrTrackingArea> area(
          [[CrTrackingArea alloc] initWithRect:decorationFrames[i]
                                       options:NSTrackingMouseEnteredAndExited |
                                               NSTrackingActiveAlways
                                         owner:view
                                      userInfo:info]);
      [view addTrackingArea:area];
    }
  }
}

// Given a newly created .webloc plist url file, also give it a resource
// fork and insert 'TEXT and 'url ' resources holding further copies of the
// url data. This is required for apps such as Terminal and Safari to accept it
// as a real webloc file when dragged in.
// It's expected that the resource fork requirement will go away at some
// point and this code can then be deleted.
OSErr WriteURLToNewWebLocFileResourceFork(NSURL* file, NSString* urlStr) {
  ResFileRefNum refNum = kResFileNotOpened;
  ResFileRefNum prevResRef = CurResFile();
  FSRef fsRef;
  OSErr err = noErr;
  HFSUniStr255 resourceForkName;
  FSGetResourceForkName(&resourceForkName);

  if (![[NSFileManager defaultManager] fileExistsAtPath:[file path]])
    return fnfErr;

  if (!CFURLGetFSRef((CFURLRef)file, &fsRef))
    return fnfErr;

  err = FSCreateResourceFork(&fsRef,
                             resourceForkName.length,
                             resourceForkName.unicode,
                             0);
  if (err)
    return err;
  err = FSOpenResourceFile(&fsRef,
                           resourceForkName.length,
                           resourceForkName.unicode,
                           fsRdWrPerm, &refNum);
  if (err)
    return err;

  const char* utf8URL = [urlStr UTF8String];
  int urlChars = strlen(utf8URL);

  Handle urlHandle = NewHandle(urlChars);
  memcpy(*urlHandle, utf8URL, urlChars);

  Handle textHandle = NewHandle(urlChars);
  memcpy(*textHandle, utf8URL, urlChars);

  // Data for the 'drag' resource.
  // This comes from derezzing webloc files made by the Finder.
  // It's bigendian data, so it's represented here as chars to preserve
  // byte order.
  char dragData[] = {
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, // Header.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x54, 0x45, 0x58, 0x54, 0x00, 0x00, 0x01, 0x00, // 'TEXT', 0, 256
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x72, 0x6C, 0x20, 0x00, 0x00, 0x01, 0x00, // 'url ', 0, 256
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  Handle dragHandle = NewHandleClear(sizeof(dragData));
  memcpy(*dragHandle, &dragData[0], sizeof(dragData));

  // Save the resources to the file.
  ConstStr255Param noName = {0};
  AddResource(dragHandle, 'drag', 128, noName);
  AddResource(textHandle, 'TEXT', 256, noName);
  AddResource(urlHandle, 'url ', 256, noName);

  CloseResFile(refNum);
  UseResFile(prevResRef);
  return noErr;
}

// Returns the file path for file |name| if saved at NSURL |base|.
static NSString* PathWithBaseURLAndName(NSURL* base, NSString* name) {
  NSString* filteredName =
      [name stringByAddingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
  return [[NSURL URLWithString:filteredName relativeToURL:base] path];
}

// Returns if there is already a file |name| at dir NSURL |base|.
static BOOL FileAlreadyExists(NSURL* base, NSString* name) {
  NSString* path = PathWithBaseURLAndName(base, name);
  DCHECK([path hasSuffix:name]);
  return [[NSFileManager defaultManager] fileExistsAtPath:path];
}

// Takes a destination URL, a suggested file name, & an extension (eg .webloc).
// Returns the complete file name with extension you should use.
// The name returned will not contain /, : or ?, will not be longer than
// kMaxNameLength + length of extension, and will not be a file name that
// already exists in that directory. If necessary it will try appending a space
// and a number to the name (but before the extension) trying numbers up to and
// including kMaxIndex.
// If the function gives up it returns nil.
static NSString* UnusedLegalNameForNewDropFile(NSURL* saveLocation,
                                               NSString *fileName,
                                               NSString *extension) {
  int number = 1;
  const int kMaxIndex = 20;
  const unsigned kMaxNameLength = 64; // Arbitrary.

  NSString* filteredName = [fileName stringByReplacingOccurrencesOfString:@"/"
                                                               withString:@"-"];
  filteredName = [filteredName stringByReplacingOccurrencesOfString:@":"
                                                         withString:@"-"];
  filteredName = [filteredName stringByReplacingOccurrencesOfString:@"?"
                                                         withString:@"-"];

  if ([filteredName length] > kMaxNameLength)
    filteredName = [filteredName substringToIndex:kMaxNameLength];

  NSString* candidateName = [filteredName stringByAppendingString:extension];

  while (FileAlreadyExists(saveLocation, candidateName)) {
    if (number > kMaxIndex)
      return nil;
    else
      candidateName = [filteredName stringByAppendingFormat:@" %d%@",
                       number++, extension];
  }

  return candidateName;
}

- (NSArray*)namesOfPromisedFilesDroppedAtDestination:(NSURL*)dropDestination {
  NSPasteboard* pboard = [NSPasteboard pasteboardWithName:NSDragPboard];
  NSFileManager* fileManager = [NSFileManager defaultManager];

  if (![pboard containsURLData])
    return NULL;

  NSArray *urls = NULL;
  NSArray* titles = NULL;
  [pboard getURLs:&urls andTitles:&titles convertingFilenames:YES];

  NSString* urlStr = [urls objectAtIndex:0];
  NSString* nameStr = [titles objectAtIndex:0];

  NSString* nameWithExtensionStr =
      UnusedLegalNameForNewDropFile(dropDestination, nameStr, @".webloc");
  if (!nameWithExtensionStr)
    return NULL;

  NSDictionary* urlDict = [NSDictionary dictionaryWithObject:urlStr
                                                      forKey:@"URL"];
  NSURL* outputURL =
      [NSURL fileURLWithPath:PathWithBaseURLAndName(dropDestination,
                                                    nameWithExtensionStr)];
  [urlDict writeToURL:outputURL
           atomically:NO];

  if (![fileManager fileExistsAtPath:[outputURL path]])
    return NULL;

  NSDictionary* attr = [NSDictionary dictionaryWithObjectsAndKeys:
      [NSNumber numberWithBool:YES], NSFileExtensionHidden,
      [NSNumber numberWithUnsignedLong:'ilht'], NSFileHFSTypeCode,
      [NSNumber numberWithUnsignedLong:'MACS'], NSFileHFSCreatorCode,
      nil];
  [fileManager setAttributes:attr
                ofItemAtPath:[outputURL path]
                       error:nil];
  // Add resource data.
  OSErr resStatus = WriteURLToNewWebLocFileResourceFork(outputURL, urlStr);
  OSSTATUS_DCHECK(resStatus == noErr, resStatus);

  return [NSArray arrayWithObject:nameWithExtensionStr];
}

- (NSDragOperation)draggingSourceOperationMaskForLocal:(BOOL)isLocal {
  return NSDragOperationCopy;
}

- (void)updateToolTipsInRect:(NSRect)cellFrame
                      ofView:(AutocompleteTextField*)controlView {
  std::vector<LocationBarDecoration*> decorations;
  std::vector<NSRect> decorationFrames;
  NSRect textFrame;
  CalculatePositionsInFrame(cellFrame, leftDecorations_, rightDecorations_,
                            &decorations, &decorationFrames, &textFrame);

  for (size_t i = 0; i < decorations.size(); ++i) {
    NSString* tooltip = decorations[i]->GetToolTip();
    if ([tooltip length] > 0)
      [controlView addToolTip:tooltip forRect:decorationFrames[i]];
  }
}

@end
