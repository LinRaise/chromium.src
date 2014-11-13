// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * The root of the file manager's view managing the DOM of Files.app.
 *
 * @param {!HTMLElement} element Top level element of Files.app.
 * @param {!LaunchParam} launchParam Launch param.
 * @constructor
 * @struct
 */
function FileManagerUI(element, launchParam) {
  // Pre-populate the static localized strings.
  i18nTemplate.process(element.ownerDocument, loadTimeData);

  // Initialize the dialog label. This should be done before constructing dialog
  // instances.
  cr.ui.dialogs.BaseDialog.OK_LABEL = str('OK_LABEL');
  cr.ui.dialogs.BaseDialog.CANCEL_LABEL = str('CANCEL_LABEL');

  /**
   * Top level element of Files.app.
   * @type {!HTMLElement}
   * @private
   */
  this.element_ = element;

  /**
   * Dialog type.
   * @type {DialogType}
   * @private
   */
  this.dialogType_ = launchParam.type;

  /**
   * Error dialog.
   * @type {!ErrorDialog}
   * @const
   */
  this.errorDialog = new ErrorDialog(this.element_);

  /**
   * Alert dialog.
   * @type {!cr.ui.dialogs.AlertDialog}
   * @const
   */
  this.alertDialog = new cr.ui.dialogs.AlertDialog(this.element_);

  /**
   * Confirm dialog.
   * @type {!cr.ui.dialogs.ConfirmDialog}
   * @const
   */
  this.confirmDialog = new cr.ui.dialogs.ConfirmDialog(this.element_);

  /**
   * Confirm dialog for delete.
   * @type {!cr.ui.dialogs.ConfirmDialog}
   * @const
   */
  this.deleteConfirmDialog = new cr.ui.dialogs.ConfirmDialog(this.element_);
  this.deleteConfirmDialog.setOkLabel(str('DELETE_BUTTON_LABEL'));

  /**
   * Prompt dialog.
   * @type {!cr.ui.dialogs.PromptDialog}
   * @const
   */
  this.promptDialog = new cr.ui.dialogs.PromptDialog(this.element_);

  /**
   * Share dialog.
   * @type {!ShareDialog}
   * @const
   */
  this.shareDialog = new ShareDialog(this.element_);

  /**
   * Multi-profile share dialog.
   * @type {!MultiProfileShareDialog}
   * @const
   */
  this.multiProfileShareDialog = new MultiProfileShareDialog(this.element_);

  /**
   * Default task picker.
   * @type {!cr.filebrowser.DefaultActionDialog}
   * @const
   */
  this.defaultTaskPicker =
      new cr.filebrowser.DefaultActionDialog(this.element_);

  /**
   * Suggest apps dialog.
   * @type {!SuggestAppsDialog}
   * @const
   */
  this.suggestAppsDialog = new SuggestAppsDialog(
      this.element_, launchParam.suggestAppsDialogState);

  /**
   * Conflict dialog.
   * @type {!ConflictDialog}
   * @const
   */
  this.conflictDialog = new ConflictDialog(this.element_);

  /**
   * Context menu for texts.
   * @type {!cr.ui.Menu}
   * @const
   */
  this.textContextMenu = FileManagerUI.queryDecoratedElement_(
      '#text-context-menu', cr.ui.Menu);

  /**
   * Location line.
   * @type {LocationLine}
   */
  this.locationLine = null;

  /**
   * Search box.
   * @type {!SearchBox}
   */
  this.searchBox = new SearchBox(
      this.element_.querySelector('#search-box'),
      this.element_.querySelector('#search-button'),
      this.element_.querySelector('#no-search-results'));

  /**
   * Toggle-view button.
   * @type {!Element}
   */
  this.toggleViewButton = queryRequiredElement(this.element_, '#view-button');

  /**
   * The button to open gear menu.
   * @type {!cr.ui.MenuButton}
   */
  this.gearButton = FileManagerUI.queryDecoratedElement_(
      '#gear-button', cr.ui.MenuButton);

  /**
   * Directory tree.
   * @type {DirectoryTree}
   */
  this.directoryTree = null;

  /**
   * Progress center panel.
   * @type {!ProgressCenterPanel}
   * @const
   */
  this.progressCenterPanel = new ProgressCenterPanel(
      queryRequiredElement(this.element_, '#progress-center'));

  /**
   * List container.
   * @type {ListContainer}
   */
  this.listContainer = null;

  /**
   * @type {PreviewPanel}
   */
  this.previewPanel = null;

  /**
   * The combo button to specify the task.
   * @type {!cr.ui.ComboButton}
   * @const
   */
  this.taskMenuButton = FileManagerUI.queryDecoratedElement_(
      '#tasks', cr.ui.ComboButton);

  /**
   * Dialog footer.
   * @type {!DialogFooter}
   */
  this.dialogFooter = DialogFooter.findDialogFooter(
      this.dialogType_, /** @type {!Document} */(this.element_.ownerDocument));

  Object.seal(this);

  // Initialize attributes.
  this.element_.querySelector('#app-name').innerText =
      chrome.runtime.getManifest().name;
  this.element_.setAttribute('type', this.dialogType_);

  // Prevent opening an URL by dropping it onto the page.
  this.element_.addEventListener('drop', function(e) {
    e.preventDefault();
  });

  // Suppresses the default context menu.
  if (util.runningInBrowser()) {
    this.element_.addEventListener('contextmenu', function(e) {
      e.preventDefault();
      e.stopPropagation();
    });
  }
}

/**
 * Obtains the element that should exist, decorates it with given type, and
 * returns it.
 * @param {string} query Query for the element.
 * @param {function(new: T, ...)} type Type used to decorate.
 * @private
 * @template T
 * @return {!T} Decorated element.
 */
FileManagerUI.queryDecoratedElement_ = function(query, type) {
  var element = queryRequiredElement(document, query);
  type.decorate(element);
  return element;
};

/**
 * Initializes here elements, which are expensive or hidden in the beginning.
 *
 * @param {!FileTable} table
 * @param {!FileGrid} grid
 * @param {!PreviewPanel} previewPanel
 * @param {!LocationLine} locationLine
 */
FileManagerUI.prototype.initAdditionalUI = function(
    table, grid, previewPanel, locationLine) {
  // Listen to drag events to hide preview panel while user is dragging files.
  // Files.app prevents default actions in 'dragstart' in some situations,
  // so we listen to 'drag' to know the list is actually being dragged.
  var draggingBound = this.onDragging_.bind(this);
  var dragEndBound = this.onDragEnd_.bind(this);
  table.list.addEventListener('drag', draggingBound);
  grid.addEventListener('drag', draggingBound);
  table.list.addEventListener('dragend', dragEndBound);
  grid.addEventListener('dragend', dragEndBound);

  // Listen to dragselection events to hide preview panel while the user is
  // selecting files by drag operation.
  table.list.addEventListener('dragselectionstart', draggingBound);
  grid.addEventListener('dragselectionstart', draggingBound);
  table.list.addEventListener('dragselectionend', dragEndBound);
  grid.addEventListener('dragselectionend', dragEndBound);

  // List container.
  this.listContainer = new ListContainer(
      queryRequiredElement(this.element_, '#list-container'), table, grid);

  // Splitter.
  this.decorateSplitter_(
      queryRequiredElement(this.element_, '#navigation-list-splitter'));

  // Preview panel.
  this.previewPanel = previewPanel;
  this.previewPanel.addEventListener(
      PreviewPanel.Event.VISIBILITY_CHANGE,
      this.onPreviewPanelVisibilityChange_.bind(this));
  this.previewPanel.initialize();

  // Location line.
  this.locationLine = locationLine;

  // Init context menus.
  var fileContextMenu = FileManagerUI.queryDecoratedElement_(
      '#file-context-menu', cr.ui.Menu);
  cr.ui.contextMenuHandler.setContextMenu(grid, fileContextMenu);
  cr.ui.contextMenuHandler.setContextMenu(table.list, fileContextMenu);
  cr.ui.contextMenuHandler.setContextMenu(
      queryRequiredElement(document, '.drive-welcome.page'),
      fileContextMenu);

  // Add handlers.
  document.defaultView.addEventListener('resize', this.relayout.bind(this));
};

/**
 * TODO(hirono): Merge the method into initAdditionalUI.
 * @param {!DirectoryTree} directoryTree
 */
FileManagerUI.prototype.initDirectoryTree = function(directoryTree) {
  this.directoryTree = directoryTree;

  // Set up the context menu for the volume/shortcut items in directory tree.
  this.directoryTree.contextMenuForRootItems =
      FileManagerUI.queryDecoratedElement_('#roots-context-menu', cr.ui.Menu);
  this.directoryTree.contextMenuForSubitems =
      FileManagerUI.queryDecoratedElement_(
          '#directory-tree-context-menu', cr.ui.Menu);

  // Visible height of the directory tree depends on the size of progress
  // center panel. When the size of progress center panel changes, directory
  // tree has to be notified to adjust its components (e.g. progress bar).
  var observer =
      new MutationObserver(directoryTree.relayout.bind(directoryTree));
  observer.observe(this.progressCenterPanel.element,
                   /** @type {MutationObserverInit} */
                   ({subtree: true, attributes: true, childList: true}));
};

/**
 * Relayouts the UI.
 */
FileManagerUI.prototype.relayout = function() {
  this.locationLine.truncate();
  // May not be available during initialization.
  if (this.listContainer.currentListType !==
      ListContainer.ListType.UNINITIALIZED) {
    this.listContainer.currentView.relayout();
  }
  if (this.directoryTree)
    this.directoryTree.relayout();
};

/**
 * Sets the current list type.
 * @param {ListContainer.ListType} listType New list type.
 */
FileManagerUI.prototype.setCurrentListType = function(listType) {
  this.listContainer.setCurrentListType(listType);

  switch (listType) {
    case ListContainer.ListType.DETAIL:
      this.toggleViewButton.classList.remove('table');
      this.toggleViewButton.classList.add('grid');
      break;

    case ListContainer.ListType.THUMBNAIL:
      this.toggleViewButton.classList.remove('grid');
      this.toggleViewButton.classList.add('table');
      break;

    default:
      assertNotReached();
      break;
  }

  this.relayout();
};

/**
 * Invoked while the drag is being performed on the list or the grid.
 * Note: this method may be called multiple times before onDragEnd_().
 * @private
 */
FileManagerUI.prototype.onDragging_ = function() {
  // On open file dialog, the preview panel is always shown.
  if (DialogType.isOpenDialog(this.dialogType_))
    return;
  this.previewPanel.visibilityType = PreviewPanel.VisibilityType.ALWAYS_HIDDEN;
};

/**
 * Invoked when the drag is ended on the list or the grid.
 * @private
 */
FileManagerUI.prototype.onDragEnd_ = function() {
  // On open file dialog, the preview panel is always shown.
  if (DialogType.isOpenDialog(this.dialogType_))
    return;
  this.previewPanel.visibilityType = PreviewPanel.VisibilityType.AUTO;
};

/**
 * Resize details and thumb views to fit the new window size.
 * @private
 */
FileManagerUI.prototype.onPreviewPanelVisibilityChange_ = function() {
  // This method may be called on initialization. Some object may not be
  // initialized.
  var panelHeight = this.previewPanel.visible ?
      this.previewPanel.height : 0;
  this.listContainer.table.setBottomMarginForPanel(panelHeight);
  this.listContainer.grid.setBottomMarginForPanel(panelHeight);
};

/**
 * Decorates the given splitter element.
 * @param {!HTMLElement} splitterElement
 * @private
 */
FileManagerUI.prototype.decorateSplitter_ = function(splitterElement) {
  var self = this;
  var Splitter = cr.ui.Splitter;
  var customSplitter = cr.ui.define('div');

  customSplitter.prototype = {
    __proto__: Splitter.prototype,

    handleSplitterDragStart: function(e) {
      Splitter.prototype.handleSplitterDragStart.apply(this, arguments);
      this.ownerDocument.documentElement.classList.add('col-resize');
    },

    handleSplitterDragMove: function(deltaX) {
      Splitter.prototype.handleSplitterDragMove.apply(this, arguments);
      self.relayout();
    },

    handleSplitterDragEnd: function(e) {
      Splitter.prototype.handleSplitterDragEnd.apply(this, arguments);
      this.ownerDocument.documentElement.classList.remove('col-resize');
    }
  };

  customSplitter.decorate(splitterElement);
};
