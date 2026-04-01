/*
 * qt_extension.cpp - Native Qt6 UI extension with dynamic loading
 *
 * Uses C ABI (HavelCAPI.h) for stability.
 * Qt6 libraries are loaded dynamically at runtime via dlopen/dlsym.
 * No hard link-time dependency on Qt6.
 */

#include "HavelCAPI.h"
#include "DynamicLoader.hpp"

// Include Qt headers for proper type information
#include <QApplication>
#include <QWidget>
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QStackedWidget>
#include <QTabWidget>
#include <QScrollArea>
#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QToolBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QColorDialog>
#include <QFontDialog>
#include <QProgressDialog>
#include <QWizard>
#include <QGroupBox>
#include <QFrame>
#include <QDockWidget>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QProgressBar>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QListWidget>
#include <QTreeWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTimer>
#include <QSettings>
#include <QClipboard>
#include <QMimeData>
#include <QStyle>
#include <QStyleFactory>
#include <QPalette>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QPixmap>
#include <QImage>
#include <QPicture>
#include <QCursor>
#include <QKeySequence>
#include <QShortcut>
#include <QFileSystemWatcher>
#include <QThread>
#include <QThreadPool>
#include <QRunnable>
#include <QSemaphore>
#include <QMutex>
#include <QMutexLocker>
#include <QReadWriteLock>
#include <QWaitCondition>

#include <unordered_map>
#include <cstdio>
#include <cstring>

namespace {

/* Qt6 opaque types */
typedef struct QWidget QWidget;
typedef struct QMainWindow QMainWindow;
typedef struct QString QString;
typedef struct QObject QObject;

/* Core function types */
typedef int (*QAppInitFn)(int* argc, char** argv);
typedef int (*QAppExecFn)();
typedef void (*QAppQuitFn)();
typedef void (*QAppProcessEventsFn)();

/* QWidget function types */
typedef QWidget* (*QWidgetCtorFn)(QWidget* parent);
typedef void (*QWidgetDtorFn)(QWidget* w);
typedef void (*QWidgetShowFn)(QWidget* w);
typedef void (*QWidgetHideFn)(QWidget* w);
typedef void (*QWidgetCloseFn)(QWidget* w);
typedef void (*QWidgetSetVisibleFn)(QWidget* w, bool v);
typedef bool (*QWidgetIsVisibleFn)(QWidget* w);
typedef void (*QWidgetSetEnabledFn)(QWidget* w, bool v);
typedef bool (*QWidgetIsEnabledFn)(QWidget* w);
typedef void (*QWidgetSetWindowTitleFn)(QWidget* w, const char* t);
typedef const char* (*QWidgetWindowTitleFn)(QWidget* w);
typedef void (*QWidgetResizeFn)(QWidget* w, int wd, int ht);
typedef void (*QWidgetMoveFn)(QWidget* w, int x, int y);
typedef void (*QWidgetSetGeometryFn)(QWidget* w, int x, int y, int wd, int ht);
typedef int (*QWidgetXFn)(QWidget* w);
typedef int (*QWidgetYFn)(QWidget* w);
typedef int (*QWidgetWidthFn)(QWidget* w);
typedef int (*QWidgetHeightFn)(QWidget* w);
typedef void (*QWidgetSetMinimumSizeFn)(QWidget* w, int wd, int ht);
typedef void (*QWidgetSetMaximumSizeFn)(QWidget* w, int wd, int ht);
typedef void (*QWidgetAdjustSizeFn)(QWidget* w);
typedef void (*QWidgetUpdateFn)(QWidget* w);
typedef void (*QWidgetSetToolTipFn)(QWidget* w, const char* tip);
typedef void (*QWidgetSetStatusTipFn)(QWidget* w, const char* tip);
typedef void (*QWidgetSetFocusFn)(QWidget* w);
typedef void (*QWidgetSetCursorFn)(QWidget* w, int cursorShape);
typedef void (*QWidgetUnsetCursorFn)(QWidget* w);
typedef void (*QWidgetSetWindowFlagsFn)(QWidget* w, int flags);
typedef void (*QWidgetSetWindowModalityFn)(QWidget* w, int modality);
typedef void (*QWidgetRaiseFn)(QWidget* w);
typedef void (*QWidgetLowerFn)(QWidget* w);
typedef void (*QWidgetActivateWindowFn)(QWidget* w);
typedef void (*QWidgetSetStyleSheetFn)(QWidget* w, const char* ss);
typedef const char* (*QWidgetStyleSheetFn)(QWidget* w);
typedef void (*QWidgetSetFontFn)(QWidget* w, void* font);
typedef void (*QWidgetSetLayoutFn)(QWidget* w, void* layout);
typedef void* (*QWidgetLayoutFn)(QWidget* w);

/* QMainWindow function types */
typedef QMainWindow* (*QMainWindowCtorFn)(QWidget* parent);
typedef void (*QMainWindowSetCentralWidgetFn)(QMainWindow* w, QWidget* cw);
typedef QWidget* (*QMainWindowCentralWidgetFn)(QMainWindow* w);
typedef void (*QMainWindowSetMenuBarFn)(QMainWindow* w, void* menubar);
typedef void (*QMainWindowSetStatusBarFn)(QMainWindow* w, void* statusbar);

/* QLabel function types */
typedef QWidget* (*QLabelCtorFn)(const char* text, QWidget* parent);
typedef void (*QLabelSetTextFn)(QWidget* l, const char* t);
typedef const char* (*QLabelTextFn)(QWidget* l);
typedef void (*QLabelSetAlignmentFn)(QWidget* l, int alignment);
typedef void (*QLabelSetWordWrapFn)(QWidget* l, bool wrap);
typedef void (*QLabelSetPixmapFn)(QWidget* l, void* pixmap);
typedef void (*QLabelClearFn)(QWidget* l);

/* QPushButton function types */
typedef QWidget* (*QPushButtonCtorFn)(const char* text, QWidget* parent);
typedef void (*QPushButtonSetTextFn)(QWidget* b, const char* t);
typedef const char* (*QPushButtonTextFn)(QWidget* b);
typedef void (*QPushButtonSetIconFn)(QWidget* b, void* icon);
typedef void (*QPushButtonSetCheckableFn)(QWidget* b, bool c);
typedef bool (*QPushButtonIsCheckableFn)(QWidget* b);
typedef void (*QPushButtonSetCheckedFn)(QWidget* b, bool c);
typedef bool (*QPushButtonIsCheckedFn)(QWidget* b);
typedef void (*QPushButtonSetDefaultFn)(QWidget* b, bool d);
typedef void (*QPushButtonSetFlatFn)(QWidget* b, bool f);
typedef void (*QPushButtonClickFn)(QWidget* b);
typedef void (*QPushButtonToggleFn)(QWidget* b);

/* QCheckBox function types */
typedef QWidget* (*QCheckBoxCtorFn)(const char* text, QWidget* parent);
typedef void (*QCheckBoxSetTextFn)(QWidget* cb, const char* t);
typedef const char* (*QCheckBoxTextFn)(QWidget* cb);
typedef void (*QCheckBoxSetCheckStateFn)(QWidget* cb, int state);
typedef int (*QCheckBoxCheckStateFn)(QWidget* cb);
typedef void (*QCheckBoxSetTristateFn)(QWidget* cb, bool t);

/* QRadioButton function types */
typedef QWidget* (*QRadioButtonCtorFn)(const char* text, QWidget* parent);
typedef void (*QRadioButtonSetTextFn)(QWidget* rb, const char* t);

/* QGroupBox function types */
typedef QWidget* (*QGroupBoxCtorFn)(const char* title, QWidget* parent);
typedef void (*QGroupBoxSetTitleFn)(QWidget* gb, const char* t);
typedef void (*QGroupBoxSetCheckableFn)(QWidget* gb, bool c);

/* QLineEdit function types */
typedef QWidget* (*QLineEditCtorFn)(const char* text, QWidget* parent);
typedef void (*QLineEditSetTextFn)(QWidget* le, const char* t);
typedef const char* (*QLineEditTextFn)(QWidget* le);
typedef void (*QLineEditSetPlaceholderTextFn)(QWidget* le, const char* t);
typedef const char* (*QLineEditPlaceholderTextFn)(QWidget* le);
typedef void (*QLineEditSetMaxLengthFn)(QWidget* le, int max);
typedef void (*QLineEditSetReadOnlyFn)(QWidget* le, bool ro);
typedef bool (*QLineEditIsReadOnlyFn)(QWidget* le);
typedef void (*QLineEditSetEchoModeFn)(QWidget* le, int mode);
typedef void (*QLineEditSetAlignmentFn)(QWidget* le, int alignment);
typedef void (*QLineEditSetFrameFn)(QWidget* le, bool frame);
typedef void (*QLineEditClearFn)(QWidget* le);
typedef void (*QLineEditDeselectFn)(QWidget* le);
typedef void (*QLineEditUndoFn)(QWidget* le);
typedef void (*QLineEditRedoFn)(QWidget* le);

/* QTextEdit function types */
typedef QWidget* (*QTextEditCtorFn)(const char* text, QWidget* parent);
typedef void (*QTextEditSetPlainTextFn)(QWidget* te, const char* t);
typedef const char* (*QTextEditToPlainTextFn)(QWidget* te);
typedef void (*QTextEditSetHtmlFn)(QWidget* te, const char* html);
typedef const char* (*QTextEditToHtmlFn)(QWidget* te);
typedef void (*QTextEditAppendFn)(QWidget* te, const char* t);
typedef void (*QTextEditClearFn)(QWidget* te);
typedef void (*QTextEditSetReadOnlyFn)(QWidget* te, bool ro);
typedef bool (*QTextEditIsReadOnlyFn)(QWidget* te);
typedef void (*QTextEditSetPlaceholderTextFn)(QWidget* te, const char* t);
typedef void (*QTextEditCopyFn)(QWidget* te);
typedef void (*QTextEditCutFn)(QWidget* te);
typedef void (*QTextEditPasteFn)(QWidget* te);

/* QComboBox function types */
typedef QWidget* (*QComboBoxCtorFn)(QWidget* parent);
typedef void (*QComboBoxAddItemFn)(QWidget* cb, const char* text, void* userData);
typedef void (*QComboBoxInsertItemFn)(QWidget* cb, int idx, const char* text, void* userData);
typedef void (*QComboBoxRemoveItemFn)(QWidget* cb, int idx);
typedef void (*QComboBoxClearFn)(QWidget* cb);
typedef int (*QComboBoxCountFn)(QWidget* cb);
typedef void (*QComboBoxSetCurrentIndexFn)(QWidget* cb, int idx);
typedef int (*QComboBoxCurrentIndexFn)(QWidget* cb);
typedef const char* (*QComboBoxCurrentTextFn)(QWidget* cb);
typedef const char* (*QComboBoxItemTextFn)(QWidget* cb, int idx);
typedef void (*QComboBoxSetEditableFn)(QWidget* cb, bool editable);
typedef void (*QComboBoxSetMaxVisibleItemsFn)(QWidget* cb, int max);

/* QSpinBox function types */
typedef QWidget* (*QSpinBoxCtorFn)(QWidget* parent);
typedef void (*QSpinBoxSetRangeFn)(QWidget* sb, int min, int max);
typedef int (*QSpinBoxMinimumFn)(QWidget* sb);
typedef int (*QSpinBoxMaximumFn)(QWidget* sb);
typedef void (*QSpinBoxSetValueFn)(QWidget* sb, int v);
typedef int (*QSpinBoxValueFn)(QWidget* sb);
typedef void (*QSpinBoxSetSingleStepFn)(QWidget* sb, int step);
typedef void (*QSpinBoxSetPrefixFn)(QWidget* sb, const char* p);
typedef void (*QSpinBoxSetSuffixFn)(QWidget* sb, const char* s);
typedef void (*QSpinBoxSetWrappingFn)(QWidget* sb, bool wrap);

/* QDoubleSpinBox function types */
typedef QWidget* (*QDoubleSpinBoxCtorFn)(QWidget* parent);
typedef void (*QDoubleSpinBoxSetRangeFn)(QWidget* sb, double min, double max);
typedef void (*QDoubleSpinBoxSetValueFn)(QWidget* sb, double v);
typedef double (*QDoubleSpinBoxValueFn)(QWidget* sb);
typedef void (*QDoubleSpinBoxSetDecimalsFn)(QWidget* sb, int decimals);

/* QSlider function types */
typedef QWidget* (*QSliderCtorFn)(int orientation, QWidget* parent);
typedef void (*QSliderSetOrientationFn)(QWidget* s, int o);
typedef void (*QSliderSetMinimumFn)(QWidget* s, int min);
typedef void (*QSliderSetMaximumFn)(QWidget* s, int max);
typedef void (*QSliderSetRangeFn)(QWidget* s, int min, int max);
typedef void (*QSliderSetValueFn)(QWidget* s, int v);
typedef int (*QSliderValueFn)(QWidget* s);
typedef void (*QSliderSetSingleStepFn)(QWidget* s, int step);
typedef void (*QSliderSetTickPositionFn)(QWidget* s, int pos);
typedef void (*QSliderSetInvertedAppearanceFn)(QWidget* s, bool inv);

/* QScrollBar function types */
typedef QWidget* (*QScrollBarCtorFn)(int orientation, QWidget* parent);
typedef void (*QScrollBarSetMinimumFn)(QWidget* sb, int min);
typedef void (*QScrollBarSetMaximumFn)(QWidget* sb, int max);
typedef void (*QScrollBarSetValueFn)(QWidget* sb, int v);
typedef int (*QScrollBarValueFn)(QWidget* sb);

/* QDial function types */
typedef QWidget* (*QDialCtorFn)(QWidget* parent);
typedef void (*QDialSetMinimumFn)(QWidget* d, int min);
typedef void (*QDialSetMaximumFn)(QWidget* d, int max);
typedef void (*QDialSetValueFn)(QWidget* d, int v);
typedef int (*QDialValueFn)(QWidget* d);
typedef void (*QDialSetWrappingFn)(QWidget* d, bool wrap);
typedef void (*QDialSetNotchesVisibleFn)(QWidget* d, bool visible);

/* QProgressBar function types */
typedef QWidget* (*QProgressBarCtorFn)(QWidget* parent);
typedef void (*QProgressBarSetRangeFn)(QWidget* pb, int min, int max);
typedef void (*QProgressBarSetValueFn)(QWidget* pb, int v);
typedef int (*QProgressBarValueFn)(QWidget* pb);
typedef void (*QProgressBarSetFormatFn)(QWidget* pb, const char* fmt);
typedef void (*QProgressBarSetOrientationFn)(QWidget* pb, int o);
typedef void (*QProgressBarSetTextVisibleFn)(QWidget* pb, bool v);
typedef void (*QProgressBarResetFn)(QWidget* pb);

/* QFrame function types */
typedef QWidget* (*QFrameCtorFn)(QWidget* parent);
typedef void (*QFrameSetFrameShapeFn)(QWidget* f, int shape);
typedef void (*QFrameSetFrameShadowFn)(QWidget* f, int shadow);
typedef void (*QFrameSetLineWidthFn)(QWidget* f, int w);
typedef void (*QFrameSetFrameStyleFn)(QWidget* f, int style);

/* QLayout function types */
typedef void (*QLayoutDtorFn)(void* l);
typedef void (*QLayoutSetContentsMarginsFn)(void* l, int lft, int top, int rgt, int btm);
typedef void (*QLayoutSetSpacingFn)(void* l, int sp);
typedef int (*QLayoutSpacingFn)(void* l);
typedef void (*QLayoutAddWidgetFn)(void* l, QWidget* w);
typedef void (*QLayoutAddLayoutFn)(void* l, void* child);
typedef void (*QLayoutSetAlignmentFn)(void* l, QWidget* w, int align);
typedef void (*QLayoutRemoveWidgetFn)(void* l, QWidget* w);

/* QVBoxLayout function types */
typedef void* (*QVBoxLayoutCtorFn)(QWidget* parent);
typedef void (*QVBoxLayoutAddStretchFn)(void* l, int stretch);
typedef void (*QVBoxLayoutAddSpacingFn)(void* l, int size);

/* QHBoxLayout function types */
typedef void* (*QHBoxLayoutCtorFn)(QWidget* parent);
typedef void (*QHBoxLayoutAddStretchFn)(void* l, int stretch);
typedef void (*QHBoxLayoutAddSpacingFn)(void* l, int size);

/* QGridLayout function types */
typedef void* (*QGridLayoutCtorFn)(QWidget* parent);
typedef void (*QGridLayoutAddWidgetFn)(void* l, QWidget* w, int row, int col, int rowSpan, int colSpan);
typedef void (*QGridLayoutAddLayoutFn)(void* l, void* other, int row, int col);
typedef void (*QGridLayoutSetRowSpacingFn)(void* l, int row, int sp);
typedef void (*QGridLayoutSetColumnSpacingFn)(void* l, int col, int sp);
typedef void (*QGridLayoutSetRowStretchFn)(void* l, int row, int stretch);
typedef void (*QGridLayoutSetColumnStretchFn)(void* l, int col, int stretch);

/* QTabWidget function types */
typedef QWidget* (*QTabWidgetCtorFn)(QWidget* parent);
typedef int (*QTabWidgetAddTabFn)(QWidget* tw, QWidget* w, const char* label);
typedef int (*QTabWidgetInsertTabFn)(QWidget* tw, int idx, QWidget* w, const char* label);
typedef void (*QTabWidgetRemoveTabFn)(QWidget* tw, int idx);
typedef void (*QTabWidgetSetCurrentIndexFn)(QWidget* tw, int idx);
typedef int (*QTabWidgetCurrentIndexFn)(QWidget* tw);
typedef QWidget* (*QTabWidgetCurrentWidgetFn)(QWidget* tw);
typedef void (*QTabWidgetSetCurrentWidgetFn)(QWidget* tw, QWidget* w);
typedef int (*QTabWidgetCountFn)(QWidget* tw);
typedef void (*QTabWidgetSetTabTextFn)(QWidget* tw, int idx, const char* label);
typedef const char* (*QTabWidgetTabTextFn)(QWidget* tw, int idx);
typedef void (*QTabWidgetSetTabEnabledFn)(QWidget* tw, int idx, bool enable);
typedef bool (*QTabWidgetIsTabEnabledFn)(QWidget* tw, int idx);
typedef void (*QTabWidgetSetTabsClosableFn)(QWidget* tw, bool closable);
typedef void (*QTabWidgetSetMovableFn)(QWidget* tw, bool movable);
typedef void (*QTabWidgetSetTabPositionFn)(QWidget* tw, int pos);
typedef void (*QTabWidgetClearFn)(QWidget* tw);

/* QStackedWidget function types */
typedef QWidget* (*QStackedWidgetCtorFn)(QWidget* parent);
typedef int (*QStackedWidgetAddWidgetFn)(QWidget* sw, QWidget* w);
typedef int (*QStackedWidgetInsertWidgetFn)(QWidget* sw, int idx, QWidget* w);
typedef void (*QStackedWidgetSetCurrentIndexFn)(QWidget* sw, int idx);
typedef int (*QStackedWidgetCurrentIndexFn)(QWidget* sw);
typedef QWidget* (*QStackedWidgetCurrentWidgetFn)(QWidget* sw);
typedef void (*QStackedWidgetSetCurrentWidgetFn)(QWidget* sw, QWidget* w);
typedef int (*QStackedWidgetCountFn)(QWidget* sw);
typedef QWidget* (*QStackedWidgetWidgetFn)(QWidget* sw, int idx);
typedef int (*QStackedWidgetIndexOfFn)(QWidget* sw, QWidget* w);
typedef void (*QStackedWidgetRemoveWidgetFn)(QWidget* sw, QWidget* w);

/* QScrollArea function types */
typedef QWidget* (*QScrollAreaCtorFn)(QWidget* parent);
typedef void (*QScrollAreaSetWidgetFn)(QWidget* sa, QWidget* w);
typedef QWidget* (*QScrollAreaWidgetFn)(QWidget* sa);
typedef void (*QScrollAreaSetWidgetResizableFn)(QWidget* sa, bool res);
typedef void (*QScrollAreaSetHScrollBarPolicyFn)(QWidget* sa, int policy);
typedef void (*QScrollAreaSetVScrollBarPolicyFn)(QWidget* sa, int policy);

/* QGroupBox function types - already defined above */

/* QSplitter function types */
typedef QWidget* (*QSplitterCtorFn)(int orientation, QWidget* parent);
typedef void (*QSplitterAddWidgetFn)(QWidget* sp, QWidget* w);
typedef void (*QSplitterSetOrientationFn)(QWidget* sp, int o);
typedef void (*QSplitterSetHandleWidthFn)(QWidget* sp, int w);

/* QMenuBar function types */
typedef void* (*QMenuBarCtorFn)(QWidget* parent);
typedef void* (*QMenuBarAddMenuFn)(QWidget* mb, const char* title);
typedef void* (*QMenuBarAddActionFn)(QWidget* mb, const char* text);

/* QMenu function types */
typedef void* (*QMenuCtorFn)(const char* title, QWidget* parent);
typedef void* (*QMenuAddActionFn)(void* m, const char* text);
typedef void* (*QMenuAddSeparatorFn)(void* m);
typedef void (*QMenuClearFn)(void* m);

/* QStatusBar function types */
typedef QWidget* (*QStatusBarCtorFn)(QWidget* parent);
typedef void (*QStatusBarShowMessageFn)(QWidget* sb, const char* msg, int timeout);
typedef void (*QStatusBarClearMessageFn)(QWidget* sb);
typedef void (*QStatusBarAddWidgetFn)(QWidget* sb, QWidget* w, int stretch);
typedef void (*QStatusBarAddPermanentWidgetFn)(QWidget* sb, QWidget* w, int stretch);

/* QMessageBox static functions */
typedef void (*QMessageBoxAboutFn)(QWidget* parent, const char* title, const char* text);
typedef void (*QMessageBoxAboutQtFn)(QWidget* parent, const char* title);
typedef int (*QMessageBoxQuestionFn)(QWidget* parent, const char* title, const char* text, int buttons, int defaultBtn);
typedef int (*QMessageBoxInformationFn)(QWidget* parent, const char* title, const char* text, int buttons, int defaultBtn);
typedef int (*QMessageBoxWarningFn)(QWidget* parent, const char* title, const char* text, int buttons, int defaultBtn);
typedef int (*QMessageBoxCriticalFn)(QWidget* parent, const char* title, const char* text, int buttons, int defaultBtn);

/* QInputDialog static functions */
typedef const char* (*QInputDialogGetTextFn)(QWidget* parent, const char* title, const char* label, int echo, const char* text, bool* ok, int flags);
typedef int (*QInputDialogGetIntegerFn)(QWidget* parent, const char* title, const char* label, bool* ok, int val, int min, int max, int step, int flags);

/* QFileDialog static functions */
typedef const char* (*QFileDialogGetOpenFileNameFn)(QWidget* parent, const char* caption, const char* dir, const char* filter, char** selFilter, int options);
typedef const char* (*QFileDialogGetSaveFileNameFn)(QWidget* parent, const char* caption, const char* dir, const char* filter, char** selFilter, int options);
typedef const char* (*QFileDialogGetExistingDirectoryFn)(QWidget* parent, const char* caption, const char* dir, int options);

/* QColorDialog static functions */
typedef const char* (*QColorDialogGetColorFn)(bool* ok, const char* initial, QWidget* parent, const char* title, int options);

/* QFontDialog static functions */
typedef const char* (*QFontDialogGetFontFn)(bool* ok, const char* initial, QWidget* parent, const char* title, int options);

/* QClipboard function types */
typedef void* (*QAppClipboardFn)();
typedef const char* (*QClipboardTextFn)(void* cb, int mode);
typedef void (*QClipboardSetTextFn)(void* cb, const char* text, int mode);
typedef void (*QClipboardClearFn)(void* cb);

/* QTimer function types */
typedef void* (*QTimerCtorFn)(QObject* parent);
typedef void (*QTimerStartFn)(void* t, int msec);
typedef void (*QTimerStopFn)(void* t);
typedef bool (*QTimerIsActiveFn)(void* t);
typedef void (*QTimerSetIntervalFn)(void* t, int msec);
typedef void (*QTimerSetSingleShotFn)(void* t, bool ss);

/* QSettings function types */
typedef void* (*QSettingsCtorFn)(const char* fileName, int format, QObject* parent);
typedef void (*QSettingsDtorFn)(void* s);
typedef void (*QSettingsSetValueFn)(void* s, const char* key, const char* val);
typedef const char* (*QSettingsValueFn)(void* s, const char* key, const char* defVal);
typedef bool (*QSettingsContainsFn)(void* s, const char* key);
typedef void (*QSettingsRemoveFn)(void* s, const char* key);
typedef void (*QSettingsSyncFn)(void* s);
typedef QSettingsSyncFn QsyncFn;

/* QCoreApplication functions */
typedef const char* (*QAppApplicationNameFn)();
typedef void (*QAppSetApplicationNameFn)(const char* name);
typedef const char* (*QAppOrganizationNameFn)();
typedef void (*QAppSetOrganizationNameFn)(const char* name);
typedef void (*QAppExitFn)(int code);
typedef void (*QAppFlushFn)();

/* Qt6 constants */
enum {
    Qt_AlignLeft = 0x0001, Qt_AlignRight = 0x0002, Qt_AlignHCenter = 0x0004,
    Qt_AlignTop = 0x0020, Qt_AlignBottom = 0x0040, Qt_AlignVCenter = 0x0080,
    Qt_AlignCenter = 0x0084,
    Qt_OrientationHorizontal = 0, Qt_OrientationVertical = 1,
    Qt_NoFrame = 0, Qt_Box = 1, Qt_Panel = 2, Qt_StyledPanel = 6,
    Qt_Plain = 0, Qt_Shadow = 1, Qt_Raised = 2, Qt_Sunken = 3,
    Qt_NoFocus = 0, Qt_TabFocus = 0x1, Qt_ClickFocus = 0x2, Qt_StrongFocus = 0x3,
    Qt_ArrowCursor = 0, Qt_IBeamCursor = 4, Qt_WaitCursor = 3, Qt_CrossCursor = 2,
    Qt_Window = 0x00000001, Qt_Dialog = 0x00000002, Qt_Popup = 0x00000010,
    Qt_Tool = 0x00000020, Qt_FramelessWindowHint = 0x08000000,
    Qt_WindowStaysOnTopHint = 0x00000800,
    Qt_NonModal = 0, Qt_WindowModal = 1, Qt_ApplicationModal = 2,
    Qt_NoButton = 0x0, Qt_Ok = 0x00000400, Qt_Cancel = 0x00400000,
    Qt_Yes = 0x00002000, Qt_No = 0x00004000,
    Qt_NoIcon = 0, Qt_Info = 1, Qt_Warning = 2, Qt_Critical = 3, Qt_Question = 4,
    Qt_NormalEcho = 0, Qt_NoEcho = 1, Qt_PasswordEcho = 2,
    Qt_ScrollBarAsNeeded = 0, Qt_ScrollBarAlwaysOff = 1, Qt_ScrollBarAlwaysOn = 2,
    Qt_NoTabBar = 0, Qt_TabAtTop = 1, Qt_TabAtBottom = 2, Qt_TabAtLeft = 3, Qt_TabAtRight = 4,
    Qt_IniFormat = 1, Qt_NativeFormat = 2,
};

/* Dynamic loader for Qt6 */
struct Qt6Libs {
    DynamicLoader qtCore;
    DynamicLoader qtGui;
    DynamicLoader qtWidgets;
    
    /* Core functions */
    QAppInitFn qApp_init = nullptr;
    QAppExecFn qApp_exec = nullptr;
    QAppQuitFn qApp_quit = nullptr;
    QAppProcessEventsFn qApp_processEvents = nullptr;
    QAppApplicationNameFn qApp_applicationName = nullptr;
    QAppSetApplicationNameFn qApp_setApplicationName = nullptr;
    QAppOrganizationNameFn qApp_organizationName = nullptr;
    QAppSetOrganizationNameFn qApp_setOrganizationName = nullptr;
    QAppExitFn qApp_exit = nullptr;
    QAppFlushFn qApp_flush = nullptr;
    QAppClipboardFn qApp_clipboard = nullptr;
    
    /* QWidget functions */
    QWidgetCtorFn qWidget_new = nullptr;
    QWidgetDtorFn qWidget_delete = nullptr;
    QWidgetShowFn qWidget_show = nullptr;
    QWidgetHideFn qWidget_hide = nullptr;
    QWidgetCloseFn qWidget_close = nullptr;
    QWidgetSetVisibleFn qWidget_setVisible = nullptr;
    QWidgetIsVisibleFn qWidget_isVisible = nullptr;
    QWidgetSetEnabledFn qWidget_setEnabled = nullptr;
    QWidgetIsEnabledFn qWidget_isEnabled = nullptr;
    QWidgetSetWindowTitleFn qWidget_setWindowTitle = nullptr;
    QWidgetWindowTitleFn qWidget_windowTitle = nullptr;
    QWidgetResizeFn qWidget_resize = nullptr;
    QWidgetMoveFn qWidget_move = nullptr;
    QWidgetSetGeometryFn qWidget_setGeometry = nullptr;
    QWidgetXFn qWidget_x = nullptr;
    QWidgetYFn qWidget_y = nullptr;
    QWidgetWidthFn qWidget_width = nullptr;
    QWidgetHeightFn qWidget_height = nullptr;
    QWidgetSetMinimumSizeFn qWidget_setMinimumSize = nullptr;
    QWidgetSetMaximumSizeFn qWidget_setMaximumSize = nullptr;
    QWidgetAdjustSizeFn qWidget_adjustSize = nullptr;
    QWidgetUpdateFn qWidget_update = nullptr;
    QWidgetSetToolTipFn qWidget_setToolTip = nullptr;
    QWidgetSetStatusTipFn qWidget_setStatusTip = nullptr;
    QWidgetSetFocusFn qWidget_setFocus = nullptr;
    QWidgetSetCursorFn qWidget_setCursor = nullptr;
    QWidgetUnsetCursorFn qWidget_unsetCursor = nullptr;
    QWidgetSetWindowFlagsFn qWidget_setWindowFlags = nullptr;
    QWidgetSetWindowModalityFn qWidget_setWindowModality = nullptr;
    QWidgetRaiseFn qWidget_raise = nullptr;
    QWidgetLowerFn qWidget_lower = nullptr;
    QWidgetActivateWindowFn qWidget_activateWindow = nullptr;
    QWidgetSetStyleSheetFn qWidget_setStyleSheet = nullptr;
    QWidgetStyleSheetFn qWidget_styleSheet = nullptr;
    QWidgetSetFontFn qWidget_setFont = nullptr;
    QWidgetSetLayoutFn qWidget_setLayout = nullptr;
    QWidgetLayoutFn qWidget_layout = nullptr;
    
    /* QMainWindow functions */
    QMainWindowCtorFn qMainWindow_new = nullptr;
    QMainWindowSetCentralWidgetFn qMainWindow_setCentralWidget = nullptr;
    QMainWindowCentralWidgetFn qMainWindow_centralWidget = nullptr;
    QMainWindowSetMenuBarFn qMainWindow_setMenuBar = nullptr;
    QMainWindowSetStatusBarFn qMainWindow_setStatusBar = nullptr;
    
    /* QLabel functions */
    QLabelCtorFn qLabel_new = nullptr;
    QLabelSetTextFn qLabel_setText = nullptr;
    QLabelTextFn qLabel_text = nullptr;
    QLabelSetAlignmentFn qLabel_setAlignment = nullptr;
    QLabelSetWordWrapFn qLabel_setWordWrap = nullptr;
    QLabelSetPixmapFn qLabel_setPixmap = nullptr;
    QLabelClearFn qLabel_clear = nullptr;
    
    /* QPushButton functions */
    QPushButtonCtorFn qPushButton_new = nullptr;
    QPushButtonSetTextFn qPushButton_setText = nullptr;
    QPushButtonTextFn qPushButton_text = nullptr;
    QPushButtonSetIconFn qPushButton_setIcon = nullptr;
    QPushButtonSetCheckableFn qPushButton_setCheckable = nullptr;
    QPushButtonIsCheckableFn qPushButton_isCheckable = nullptr;
    QPushButtonSetCheckedFn qPushButton_setChecked = nullptr;
    QPushButtonIsCheckedFn qPushButton_isChecked = nullptr;
    QPushButtonSetDefaultFn qPushButton_setDefault = nullptr;
    QPushButtonSetFlatFn qPushButton_setFlat = nullptr;
    QPushButtonClickFn qPushButton_click = nullptr;
    QPushButtonToggleFn qPushButton_toggle = nullptr;
    
    /* QCheckBox functions */
    QCheckBoxCtorFn qCheckBox_new = nullptr;
    QCheckBoxSetTextFn qCheckBox_setText = nullptr;
    QCheckBoxTextFn qCheckBox_text = nullptr;
    QCheckBoxSetCheckStateFn qCheckBox_setCheckState = nullptr;
    QCheckBoxCheckStateFn qCheckBox_checkState = nullptr;
    QCheckBoxSetTristateFn qCheckBox_setTristate = nullptr;
    
    /* QRadioButton functions */
    QRadioButtonCtorFn qRadioButton_new = nullptr;
    QRadioButtonSetTextFn qRadioButton_setText = nullptr;
    
    /* QGroupBox functions */
    QGroupBoxCtorFn qGroupBox_new = nullptr;
    QGroupBoxSetTitleFn qGroupBox_setTitle = nullptr;
    QGroupBoxSetCheckableFn qGroupBox_setCheckable = nullptr;
    
    /* QLineEdit functions */
    QLineEditCtorFn qLineEdit_new = nullptr;
    QLineEditSetTextFn qLineEdit_setText = nullptr;
    QLineEditTextFn qLineEdit_text = nullptr;
    QLineEditSetPlaceholderTextFn qLineEdit_setPlaceholderText = nullptr;
    QLineEditPlaceholderTextFn qLineEdit_placeholderText = nullptr;
    QLineEditSetMaxLengthFn qLineEdit_setMaxLength = nullptr;
    QLineEditSetReadOnlyFn qLineEdit_setReadOnly = nullptr;
    QLineEditIsReadOnlyFn qLineEdit_isReadOnly = nullptr;
    QLineEditSetEchoModeFn qLineEdit_setEchoMode = nullptr;
    QLineEditSetAlignmentFn qLineEdit_setAlignment = nullptr;
    QLineEditSetFrameFn qLineEdit_setFrame = nullptr;
    QLineEditClearFn qLineEdit_clear = nullptr;
    QLineEditDeselectFn qLineEdit_deselect = nullptr;
    QLineEditUndoFn qLineEdit_undo = nullptr;
    QLineEditRedoFn qLineEdit_redo = nullptr;
    
    /* QTextEdit functions */
    QTextEditCtorFn qTextEdit_new = nullptr;
    QTextEditSetPlainTextFn qTextEdit_setPlainText = nullptr;
    QTextEditToPlainTextFn qTextEdit_toPlainText = nullptr;
    QTextEditSetHtmlFn qTextEdit_setHtml = nullptr;
    QTextEditToHtmlFn qTextEdit_toHtml = nullptr;
    QTextEditAppendFn qTextEdit_append = nullptr;
    QTextEditClearFn qTextEdit_clear = nullptr;
    QTextEditSetReadOnlyFn qTextEdit_setReadOnly = nullptr;
    QTextEditIsReadOnlyFn qTextEdit_isReadOnly = nullptr;
    QTextEditSetPlaceholderTextFn qTextEdit_setPlaceholderText = nullptr;
    QTextEditCopyFn qTextEdit_copy = nullptr;
    QTextEditCutFn qTextEdit_cut = nullptr;
    QTextEditPasteFn qTextEdit_paste = nullptr;
    
    /* QComboBox functions */
    QComboBoxCtorFn qComboBox_new = nullptr;
    QComboBoxAddItemFn qComboBox_addItem = nullptr;
    QComboBoxInsertItemFn qComboBox_insertItem = nullptr;
    QComboBoxRemoveItemFn qComboBox_removeItem = nullptr;
    QComboBoxClearFn qComboBox_clear = nullptr;
    QComboBoxCountFn qComboBox_count = nullptr;
    QComboBoxSetCurrentIndexFn qComboBox_setCurrentIndex = nullptr;
    QComboBoxCurrentIndexFn qComboBox_currentIndex = nullptr;
    QComboBoxCurrentTextFn qComboBox_currentText = nullptr;
    QComboBoxItemTextFn qComboBox_itemText = nullptr;
    QComboBoxSetEditableFn qComboBox_setEditable = nullptr;
    QComboBoxSetMaxVisibleItemsFn qComboBox_setMaxVisibleItems = nullptr;
    
    /* QSpinBox functions */
    QSpinBoxCtorFn qSpinBox_new = nullptr;
    QSpinBoxSetRangeFn qSpinBox_setRange = nullptr;
    QSpinBoxMinimumFn qSpinBox_minimum = nullptr;
    QSpinBoxMaximumFn qSpinBox_maximum = nullptr;
    QSpinBoxSetValueFn qSpinBox_setValue = nullptr;
    QSpinBoxValueFn qSpinBox_value = nullptr;
    QSpinBoxSetSingleStepFn qSpinBox_setSingleStep = nullptr;
    QSpinBoxSetPrefixFn qSpinBox_setPrefix = nullptr;
    QSpinBoxSetSuffixFn qSpinBox_setSuffix = nullptr;
    QSpinBoxSetWrappingFn qSpinBox_setWrapping = nullptr;
    
    /* QDoubleSpinBox functions */
    QDoubleSpinBoxCtorFn qDoubleSpinBox_new = nullptr;
    QDoubleSpinBoxSetRangeFn qDoubleSpinBox_setRange = nullptr;
    QDoubleSpinBoxSetValueFn qDoubleSpinBox_setValue = nullptr;
    QDoubleSpinBoxValueFn qDoubleSpinBox_value = nullptr;
    QDoubleSpinBoxSetDecimalsFn qDoubleSpinBox_setDecimals = nullptr;
    
    /* QSlider functions */
    QSliderCtorFn qSlider_new = nullptr;
    QSliderSetOrientationFn qSlider_setOrientation = nullptr;
    QSliderSetMinimumFn qSlider_setMinimum = nullptr;
    QSliderSetMaximumFn qSlider_setMaximum = nullptr;
    QSliderSetRangeFn qSlider_setRange = nullptr;
    QSliderSetValueFn qSlider_setValue = nullptr;
    QSliderValueFn qSlider_value = nullptr;
    QSliderSetSingleStepFn qSlider_setSingleStep = nullptr;
    QSliderSetTickPositionFn qSlider_setTickPosition = nullptr;
    QSliderSetInvertedAppearanceFn qSlider_setInvertedAppearance = nullptr;
    
    /* QScrollBar functions */
    QScrollBarCtorFn qScrollBar_new = nullptr;
    QScrollBarSetMinimumFn qScrollBar_setMinimum = nullptr;
    QScrollBarSetMaximumFn qScrollBar_setMaximum = nullptr;
    QScrollBarSetValueFn qScrollBar_setValue = nullptr;
    QScrollBarValueFn qScrollBar_value = nullptr;
    
    /* QDial functions */
    QDialCtorFn qDial_new = nullptr;
    QDialSetMinimumFn qDial_setMinimum = nullptr;
    QDialSetMaximumFn qDial_setMaximum = nullptr;
    QDialSetValueFn qDial_setValue = nullptr;
    QDialValueFn qDial_value = nullptr;
    QDialSetWrappingFn qDial_setWrapping = nullptr;
    QDialSetNotchesVisibleFn qDial_setNotchesVisible = nullptr;
    
    /* QProgressBar functions */
    QProgressBarCtorFn qProgressBar_new = nullptr;
    QProgressBarSetRangeFn qProgressBar_setRange = nullptr;
    QProgressBarSetValueFn qProgressBar_setValue = nullptr;
    QProgressBarValueFn qProgressBar_value = nullptr;
    QProgressBarSetFormatFn qProgressBar_setFormat = nullptr;
    QProgressBarSetOrientationFn qProgressBar_setOrientation = nullptr;
    QProgressBarSetTextVisibleFn qProgressBar_setTextVisible = nullptr;
    QProgressBarResetFn qProgressBar_reset = nullptr;
    
    /* QFrame functions */
    QFrameCtorFn qFrame_new = nullptr;
    QFrameSetFrameShapeFn qFrame_setFrameShape = nullptr;
    QFrameSetFrameShadowFn qFrame_setFrameShadow = nullptr;
    QFrameSetLineWidthFn qFrame_setLineWidth = nullptr;
    QFrameSetFrameStyleFn qFrame_setFrameStyle = nullptr;
    
    /* Layout functions */
    QLayoutDtorFn qLayout_delete = nullptr;
    QLayoutSetContentsMarginsFn qLayout_setContentsMargins = nullptr;
    QLayoutSetSpacingFn qLayout_setSpacing = nullptr;
    QLayoutSpacingFn qLayout_spacing = nullptr;
    QLayoutAddWidgetFn qLayout_addWidget = nullptr;
    QLayoutAddLayoutFn qLayout_addLayout = nullptr;
    QLayoutSetAlignmentFn qLayout_setAlignment = nullptr;
    QLayoutRemoveWidgetFn qLayout_removeWidget = nullptr;
    
    QVBoxLayoutCtorFn qVBoxLayout_new = nullptr;
    QVBoxLayoutAddStretchFn qVBoxLayout_addStretch = nullptr;
    QVBoxLayoutAddSpacingFn qVBoxLayout_addSpacing = nullptr;
    
    QHBoxLayoutCtorFn qHBoxLayout_new = nullptr;
    QHBoxLayoutAddStretchFn qHBoxLayout_addStretch = nullptr;
    QHBoxLayoutAddSpacingFn qHBoxLayout_addSpacing = nullptr;
    
    QGridLayoutCtorFn qGridLayout_new = nullptr;
    QGridLayoutAddWidgetFn qGridLayout_addWidget = nullptr;
    QGridLayoutAddLayoutFn qGridLayout_addLayout = nullptr;
    QGridLayoutSetRowSpacingFn qGridLayout_setRowSpacing = nullptr;
    QGridLayoutSetColumnSpacingFn qGridLayout_setColumnSpacing = nullptr;
    QGridLayoutSetRowStretchFn qGridLayout_setRowStretch = nullptr;
    QGridLayoutSetColumnStretchFn qGridLayout_setColumnStretch = nullptr;
    
    /* QTabWidget functions */
    QTabWidgetCtorFn qTabWidget_new = nullptr;
    QTabWidgetAddTabFn qTabWidget_addTab = nullptr;
    QTabWidgetInsertTabFn qTabWidget_insertTab = nullptr;
    QTabWidgetRemoveTabFn qTabWidget_removeTab = nullptr;
    QTabWidgetSetCurrentIndexFn qTabWidget_setCurrentIndex = nullptr;
    QTabWidgetCurrentIndexFn qTabWidget_currentIndex = nullptr;
    QTabWidgetCurrentWidgetFn qTabWidget_currentWidget = nullptr;
    QTabWidgetSetCurrentWidgetFn qTabWidget_setCurrentWidget = nullptr;
    QTabWidgetCountFn qTabWidget_count = nullptr;
    QTabWidgetSetTabTextFn qTabWidget_setTabText = nullptr;
    QTabWidgetTabTextFn qTabWidget_tabText = nullptr;
    QTabWidgetSetTabEnabledFn qTabWidget_setTabEnabled = nullptr;
    QTabWidgetIsTabEnabledFn qTabWidget_isTabEnabled = nullptr;
    QTabWidgetSetTabsClosableFn qTabWidget_setTabsClosable = nullptr;
    QTabWidgetSetMovableFn qTabWidget_setMovable = nullptr;
    QTabWidgetSetTabPositionFn qTabWidget_setTabPosition = nullptr;
    QTabWidgetClearFn qTabWidget_clear = nullptr;
    
    /* QStackedWidget functions */
    QStackedWidgetCtorFn qStackedWidget_new = nullptr;
    QStackedWidgetAddWidgetFn qStackedWidget_addWidget = nullptr;
    QStackedWidgetInsertWidgetFn qStackedWidget_insertWidget = nullptr;
    QStackedWidgetSetCurrentIndexFn qStackedWidget_setCurrentIndex = nullptr;
    QStackedWidgetCurrentIndexFn qStackedWidget_currentIndex = nullptr;
    QStackedWidgetCurrentWidgetFn qStackedWidget_currentWidget = nullptr;
    QStackedWidgetSetCurrentWidgetFn qStackedWidget_setCurrentWidget = nullptr;
    QStackedWidgetCountFn qStackedWidget_count = nullptr;
    QStackedWidgetWidgetFn qStackedWidget_widget = nullptr;
    QStackedWidgetIndexOfFn qStackedWidget_indexOf = nullptr;
    QStackedWidgetRemoveWidgetFn qStackedWidget_removeWidget = nullptr;
    
    /* QScrollArea functions */
    QScrollAreaCtorFn qScrollArea_new = nullptr;
    QScrollAreaSetWidgetFn qScrollArea_setWidget = nullptr;
    QScrollAreaWidgetFn qScrollArea_widget = nullptr;
    QScrollAreaSetWidgetResizableFn qScrollArea_setWidgetResizable = nullptr;
    QScrollAreaSetHScrollBarPolicyFn qScrollArea_setHScrollBarPolicy = nullptr;
    QScrollAreaSetVScrollBarPolicyFn qScrollArea_setVScrollBarPolicy = nullptr;
    
    /* QSplitter functions */
    QSplitterCtorFn qSplitter_new = nullptr;
    QSplitterAddWidgetFn qSplitter_addWidget = nullptr;
    QSplitterSetOrientationFn qSplitter_setOrientation = nullptr;
    QSplitterSetHandleWidthFn qSplitter_setHandleWidth = nullptr;
    
    /* QMenuBar functions */
    QMenuBarCtorFn qMenuBar_new = nullptr;
    QMenuBarAddMenuFn qMenuBar_addMenu = nullptr;
    QMenuBarAddActionFn qMenuBar_addAction = nullptr;
    
    /* QMenu functions */
    QMenuCtorFn qMenu_new = nullptr;
    QMenuAddActionFn qMenu_addAction = nullptr;
    QMenuAddSeparatorFn qMenu_addSeparator = nullptr;
    QMenuClearFn qMenu_clear = nullptr;
    
    /* QStatusBar functions */
    QStatusBarCtorFn qStatusBar_new = nullptr;
    QStatusBarShowMessageFn qStatusBar_showMessage = nullptr;
    QStatusBarClearMessageFn qStatusBar_clearMessage = nullptr;
    QStatusBarAddWidgetFn qStatusBar_addWidget = nullptr;
    QStatusBarAddPermanentWidgetFn qStatusBar_addPermanentWidget = nullptr;
    
    /* QMessageBox functions */
    QMessageBoxAboutFn qMessageBox_about = nullptr;
    QMessageBoxAboutQtFn qMessageBox_aboutQt = nullptr;
    QMessageBoxQuestionFn qMessageBox_question = nullptr;
    QMessageBoxInformationFn qMessageBox_information = nullptr;
    QMessageBoxWarningFn qMessageBox_warning = nullptr;
    QMessageBoxCriticalFn qMessageBox_critical = nullptr;
    
    /* QInputDialog functions */
    QInputDialogGetTextFn qInputDialog_getText = nullptr;
    QInputDialogGetIntegerFn qInputDialog_getInteger = nullptr;
    
    /* QFileDialog functions */
    QFileDialogGetOpenFileNameFn qFileDialog_getOpenFileName = nullptr;
    QFileDialogGetSaveFileNameFn qFileDialog_getSaveFileName = nullptr;
    QFileDialogGetExistingDirectoryFn qFileDialog_getExistingDirectory = nullptr;
    
    /* QColorDialog functions */
    QColorDialogGetColorFn qColorDialog_getColor = nullptr;
    
    /* QFontDialog functions */
    QFontDialogGetFontFn qFontDialog_getFont = nullptr;
    
    /* QClipboard functions */
    QClipboardTextFn qClipboard_text = nullptr;
    QClipboardSetTextFn qClipboard_setText = nullptr;
    QClipboardClearFn qClipboard_clear = nullptr;
    
    /* QTimer functions */
    QTimerCtorFn qTimer_new = nullptr;
    QTimerStartFn qTimer_start = nullptr;
    QTimerStopFn qTimer_stop = nullptr;
    QTimerIsActiveFn qTimer_isActive = nullptr;
    QTimerSetIntervalFn qTimer_setInterval = nullptr;
    QTimerSetSingleShotFn qTimer_setSingleShot = nullptr;
    
    /* QSettings functions */
    QSettingsCtorFn qSettings_new = nullptr;
    QSettingsDtorFn qSettings_delete = nullptr;
    QSettingsSetValueFn qSettings_setValue = nullptr;
    QSettingsValueFn qSettings_value = nullptr;
    QSettingsContainsFn qSettings_contains = nullptr;
    QSettingsRemoveFn qSettings_remove = nullptr;
    QSettingsSyncFn qSettings_sync = nullptr;
    
    bool load() {
        /* Load Qt6Core */
        if (!qtCore.load("libQt6Core.so.6")) {
            if (!qtCore.load("libQt5Core.so.5")) {
                fprintf(stderr, "[Qt] Failed to load Qt Core\n");
                return false;
            }
        }
        
        /* Load Qt6Gui */
        if (!qtGui.load("libQt6Gui.so.6")) {
            if (!qtGui.load("libQt5Gui.so.5")) {
                fprintf(stderr, "[Qt] Failed to load Qt Gui\n");
                return false;
            }
        }
        
        /* Load Qt6Widgets */
        if (!qtWidgets.load("libQt6Widgets.so.6")) {
            if (!qtWidgets.load("libQt5Widgets.so.5")) {
                fprintf(stderr, "[Qt] Failed to load Qt Widgets\n");
                return false;
            }
        }
        
        fprintf(stderr, "[Qt] Qt libraries loaded dynamically\n");
        
        /* Load Core symbols */
#define LOAD_CORE(name) name = qtCore.getSymbol<Q##name##Fn>("_" QT_STRINGIFY(name)); if (!name) name = qtCore.getSymbol<Q##name##Fn>(#name);
#define LOAD(name) name = qtWidgets.getSymbol<Q##name##Fn>("_" QT_STRINGIFY(name)); if (!name) name = qtWidgets.getSymbol<Q##name##Fn>(#name);
        
        qApp_init = qtCore.getSymbol<QAppInitFn>("_ZN12QApplicationC1EPiPPci");
        qApp_exec = qtCore.getSymbol<QAppExecFn>("_ZN12QApplication4execEv");
        qApp_quit = qtCore.getSymbol<QAppQuitFn>("_ZN12QApplication4quitEv");
        qApp_processEvents = qtCore.getSymbol<QAppProcessEventsFn>("_ZN16QCoreApplication16processEventsEv");
        qApp_applicationName = qtCore.getSymbol<QAppApplicationNameFn>("_ZN16QCoreApplication14applicationNameEv");
        qApp_setApplicationName = qtCore.getSymbol<QAppSetApplicationNameFn>("_ZN16QCoreApplication19setApplicationNameERK7QString");
        qApp_organizationName = qtCore.getSymbol<QAppOrganizationNameFn>("_ZN16QCoreApplication17organizationNameEv");
        qApp_setOrganizationName = qtCore.getSymbol<QAppSetOrganizationNameFn>("_ZN16QCoreApplication22setOrganizationNameERK7QString");
        qApp_exit = qtCore.getSymbol<QAppExitFn>("_ZN16QCoreApplication4exitEi");
        qApp_flush = qtCore.getSymbol<QAppFlushFn>("_ZN16QCoreApplication5flushEv");
        qApp_clipboard = qtGui.getSymbol<QAppClipboardFn>("_ZN15QGuiApplication9clipboardEv");
        
        /* QWidget */
        qWidget_new = qtCore.getSymbol<QWidgetCtorFn>("_ZN7QWidgetC1EP7QWidget6QFlagsIN2Qt10WindowTypeEE");
        if (!qWidget_new) qWidget_new = qtCore.getSymbol<QWidgetCtorFn>("QWidget");
        qWidget_show = qtCore.getSymbol<QWidgetShowFn>("_ZN7QWidget4showEv");
        if (!qWidget_show) qWidget_show = qtCore.getSymbol<QWidgetShowFn>("show");
        qWidget_hide = qtCore.getSymbol<QWidgetHideFn>("_ZN7QWidget4hideEv");
        if (!qWidget_hide) qWidget_hide = qtCore.getSymbol<QWidgetHideFn>("hide");
        qWidget_close = qtCore.getSymbol<QWidgetCloseFn>("_ZN7QWidget5closeEv");
        if (!qWidget_close) qWidget_close = qtCore.getSymbol<QWidgetCloseFn>("close");
        qWidget_setVisible = qtCore.getSymbol<QWidgetSetVisibleFn>("_ZN7QWidget10setVisibleEb");
        if (!qWidget_setVisible) qWidget_setVisible = qtCore.getSymbol<QWidgetSetVisibleFn>("setVisible");
        qWidget_isVisible = qtCore.getSymbol<QWidgetIsVisibleFn>("_ZNK7QWidget9isVisibleEv");
        if (!qWidget_isVisible) qWidget_isVisible = qtCore.getSymbol<QWidgetIsVisibleFn>("isVisible");
        qWidget_setEnabled = qtCore.getSymbol<QWidgetSetEnabledFn>("_ZN7QWidget10setEnabledEb");
        if (!qWidget_setEnabled) qWidget_setEnabled = qtCore.getSymbol<QWidgetSetEnabledFn>("setEnabled");
        qWidget_isEnabled = qtCore.getSymbol<QWidgetIsEnabledFn>("_ZNK7QWidget9isEnabledEv");
        if (!qWidget_isEnabled) qWidget_isEnabled = qtCore.getSymbol<QWidgetIsEnabledFn>("isEnabled");
        qWidget_setWindowTitle = qtCore.getSymbol<QWidgetSetWindowTitleFn>("_ZN7QWidget14setWindowTitleERK7QString");
        if (!qWidget_setWindowTitle) qWidget_setWindowTitle = qtCore.getSymbol<QWidgetSetWindowTitleFn>("setWindowTitle");
        qWidget_windowTitle = qtCore.getSymbol<QWidgetWindowTitleFn>("_ZNK7QWidget11windowTitleEv");
        if (!qWidget_windowTitle) qWidget_windowTitle = qtCore.getSymbol<QWidgetWindowTitleFn>("windowTitle");
        qWidget_resize = qtCore.getSymbol<QWidgetResizeFn>("_ZN7QWidget6resizeEii");
        if (!qWidget_resize) qWidget_resize = qtCore.getSymbol<QWidgetResizeFn>("resize");
        qWidget_move = qtCore.getSymbol<QWidgetMoveFn>("_ZN7QWidget4moveEii");
        if (!qWidget_move) qWidget_move = qtCore.getSymbol<QWidgetMoveFn>("move");
        qWidget_setGeometry = qtCore.getSymbol<QWidgetSetGeometryFn>("_ZN7QWidget11setGeometryEiiii");
        if (!qWidget_setGeometry) qWidget_setGeometry = qtCore.getSymbol<QWidgetSetGeometryFn>("setGeometry");
        qWidget_x = qtCore.getSymbol<QWidgetXFn>("_ZNK7QWidget1xEv");
        if (!qWidget_x) qWidget_x = qtCore.getSymbol<QWidgetXFn>("x");
        qWidget_y = qtCore.getSymbol<QWidgetYFn>("_ZNK7QWidget1yEv");
        if (!qWidget_y) qWidget_y = qtCore.getSymbol<QWidgetYFn>("y");
        qWidget_width = qtCore.getSymbol<QWidgetWidthFn>("_ZNK7QWidget5widthEv");
        if (!qWidget_width) qWidget_width = qtCore.getSymbol<QWidgetWidthFn>("width");
        qWidget_height = qtCore.getSymbol<QWidgetHeightFn>("_ZNK7QWidget6heightEv");
        if (!qWidget_height) qWidget_height = qtCore.getSymbol<QWidgetHeightFn>("height");
        qWidget_setMinimumSize = qtCore.getSymbol<QWidgetSetMinimumSizeFn>("_ZN7QWidget16setMinimumSizeEii");
        if (!qWidget_setMinimumSize) qWidget_setMinimumSize = qtCore.getSymbol<QWidgetSetMinimumSizeFn>("setMinimumSize");
        qWidget_setMaximumSize = qtCore.getSymbol<QWidgetSetMaximumSizeFn>("_ZN7QWidget16setMaximumSizeEii");
        if (!qWidget_setMaximumSize) qWidget_setMaximumSize = qtCore.getSymbol<QWidgetSetMaximumSizeFn>("setMaximumSize");
        qWidget_adjustSize = qtCore.getSymbol<QWidgetAdjustSizeFn>("_ZN7QWidget10adjustSizeEv");
        if (!qWidget_adjustSize) qWidget_adjustSize = qtCore.getSymbol<QWidgetAdjustSizeFn>("adjustSize");
        qWidget_update = qtCore.getSymbol<QWidgetUpdateFn>("_ZN7QWidget6updateEv");
        if (!qWidget_update) qWidget_update = qtCore.getSymbol<QWidgetUpdateFn>("update");
        qWidget_setToolTip = qtCore.getSymbol<QWidgetSetToolTipFn>("_ZN7QWidget10setToolTipERK7QString");
        if (!qWidget_setToolTip) qWidget_setToolTip = qtCore.getSymbol<QWidgetSetToolTipFn>("setToolTip");
        qWidget_setStatusTip = qtCore.getSymbol<QWidgetSetStatusTipFn>("_ZN7QWidget12setStatusTipERK7QString");
        if (!qWidget_setStatusTip) qWidget_setStatusTip = qtCore.getSymbol<QWidgetSetStatusTipFn>("setStatusTip");
        qWidget_setFocus = qtCore.getSymbol<QWidgetSetFocusFn>("_ZN7QWidget8setFocusEv");
        if (!qWidget_setFocus) qWidget_setFocus = qtCore.getSymbol<QWidgetSetFocusFn>("setFocus");
        qWidget_setCursor = qtCore.getSymbol<QWidgetSetCursorFn>("_ZN7QWidget9setCursorERK6QCursor");
        if (!qWidget_setCursor) qWidget_setCursor = qtCore.getSymbol<QWidgetSetCursorFn>("setCursor");
        qWidget_unsetCursor = qtCore.getSymbol<QWidgetUnsetCursorFn>("_ZN7QWidget11unsetCursorEv");
        if (!qWidget_unsetCursor) qWidget_unsetCursor = qtCore.getSymbol<QWidgetUnsetCursorFn>("unsetCursor");
        qWidget_setWindowFlags = qtCore.getSymbol<QWidgetSetWindowFlagsFn>("_ZN7QWidget14setWindowFlagsE6QFlagsIN2Qt10WindowTypeEE");
        if (!qWidget_setWindowFlags) qWidget_setWindowFlags = qtCore.getSymbol<QWidgetSetWindowFlagsFn>("setWindowFlags");
        qWidget_setWindowModality = qtCore.getSymbol<QWidgetSetWindowModalityFn>("_ZN7QWidget17setWindowModalityEN2Qt14WindowModalityE");
        if (!qWidget_setWindowModality) qWidget_setWindowModality = qtCore.getSymbol<QWidgetSetWindowModalityFn>("setWindowModality");
        qWidget_raise = qtCore.getSymbol<QWidgetRaiseFn>("_ZN7QWidget5raiseEv");
        if (!qWidget_raise) qWidget_raise = qtCore.getSymbol<QWidgetRaiseFn>("raise");
        qWidget_lower = qtCore.getSymbol<QWidgetLowerFn>("_ZN7QWidget5lowerEv");
        if (!qWidget_lower) qWidget_lower = qtCore.getSymbol<QWidgetLowerFn>("lower");
        qWidget_activateWindow = qtCore.getSymbol<QWidgetActivateWindowFn>("_ZN7QWidget14activateWindowEv");
        if (!qWidget_activateWindow) qWidget_activateWindow = qtCore.getSymbol<QWidgetActivateWindowFn>("activateWindow");
        qWidget_setStyleSheet = qtCore.getSymbol<QWidgetSetStyleSheetFn>("_ZN7QWidget14setStyleSheetERK7QString");
        if (!qWidget_setStyleSheet) qWidget_setStyleSheet = qtCore.getSymbol<QWidgetSetStyleSheetFn>("setStyleSheet");
        qWidget_styleSheet = qtCore.getSymbol<QWidgetStyleSheetFn>("_ZNK7QWidget10styleSheetEv");
        if (!qWidget_styleSheet) qWidget_styleSheet = qtCore.getSymbol<QWidgetStyleSheetFn>("styleSheet");
        qWidget_setFont = qtCore.getSymbol<QWidgetSetFontFn>("_ZN7QWidget7setFontERK5QFont");
        if (!qWidget_setFont) qWidget_setFont = qtCore.getSymbol<QWidgetSetFontFn>("setFont");
        qWidget_setLayout = qtCore.getSymbol<QWidgetSetLayoutFn>("_ZN7QWidget9setLayoutEP7QLayout");
        if (!qWidget_setLayout) qWidget_setLayout = qtCore.getSymbol<QWidgetSetLayoutFn>("setLayout");
        qWidget_layout = qtCore.getSymbol<QWidgetLayoutFn>("_ZNK7QWidget6layoutEv");
        if (!qWidget_layout) qWidget_layout = qtCore.getSymbol<QWidgetLayoutFn>("layout");
        
        /* QMainWindow */
        qMainWindow_new = qtWidgets.getSymbol<QMainWindowCtorFn>("_ZN11QMainWindowC1EP7QWidget6QFlagsIN2Qt10WindowTypeEE");
        if (!qMainWindow_new) qMainWindow_new = qtWidgets.getSymbol<QMainWindowCtorFn>("QMainWindow");
        qMainWindow_setCentralWidget = qtWidgets.getSymbol<QMainWindowSetCentralWidgetFn>("_ZN11QMainWindow16setCentralWidgetEP7QWidget");
        if (!qMainWindow_setCentralWidget) qMainWindow_setCentralWidget = qtWidgets.getSymbol<QMainWindowSetCentralWidgetFn>("setCentralWidget");
        qMainWindow_centralWidget = qtWidgets.getSymbol<QMainWindowCentralWidgetFn>("_ZNK11QMainWindow13centralWidgetEv");
        if (!qMainWindow_centralWidget) qMainWindow_centralWidget = qtWidgets.getSymbol<QMainWindowCentralWidgetFn>("centralWidget");
        qMainWindow_setMenuBar = qtWidgets.getSymbol<QMainWindowSetMenuBarFn>("_ZN11QMainWindow10setMenuBarEP8QMenuBar");
        if (!qMainWindow_setMenuBar) qMainWindow_setMenuBar = qtWidgets.getSymbol<QMainWindowSetMenuBarFn>("setMenuBar");
        qMainWindow_setStatusBar = qtWidgets.getSymbol<QMainWindowSetStatusBarFn>("_ZN11QMainWindow12setStatusBarEP11QStatusBar");
        if (!qMainWindow_setStatusBar) qMainWindow_setStatusBar = qtWidgets.getSymbol<QMainWindowSetStatusBarFn>("setStatusBar");
        
        /* QLabel */
        qLabel_new = qtWidgets.getSymbol<QLabelCtorFn>("_ZN6QLabelC1EP7QWidget6QFlagsIN2Qt10WindowTypeEE");
        if (!qLabel_new) qLabel_new = qtWidgets.getSymbol<QLabelCtorFn>("QLabel");
        qLabel_setText = qtWidgets.getSymbol<QLabelSetTextFn>("_ZN6QLabel7setTextERK7QString");
        if (!qLabel_setText) qLabel_setText = qtWidgets.getSymbol<QLabelSetTextFn>("setText");
        qLabel_text = qtWidgets.getSymbol<QLabelTextFn>("_ZNK6QLabel4textEv");
        if (!qLabel_text) qLabel_text = qtWidgets.getSymbol<QLabelTextFn>("text");
        qLabel_setAlignment = qtWidgets.getSymbol<QLabelSetAlignmentFn>("_ZN6QLabel12setAlignmentE6QFlagsIN2Qt13AlignmentFlagEE");
        if (!qLabel_setAlignment) qLabel_setAlignment = qtWidgets.getSymbol<QLabelSetAlignmentFn>("setAlignment");
        qLabel_setWordWrap = qtWidgets.getSymbol<QLabelSetWordWrapFn>("_ZN6QLabel12setWordWrapEb");
        if (!qLabel_setWordWrap) qLabel_setWordWrap = qtWidgets.getSymbol<QLabelSetWordWrapFn>("setWordWrap");
        qLabel_setPixmap = qtWidgets.getSymbol<QLabelSetPixmapFn>("_ZN6QLabel10setPixmapERK7QPixmap");
        if (!qLabel_setPixmap) qLabel_setPixmap = qtWidgets.getSymbol<QLabelSetPixmapFn>("setPixmap");
        qLabel_clear = qtWidgets.getSymbol<QLabelClearFn>("_ZN6QLabel5clearEv");
        if (!qLabel_clear) qLabel_clear = qtWidgets.getSymbol<QLabelClearFn>("clear");
        
        /* QPushButton */
        qPushButton_new = qtWidgets.getSymbol<QPushButtonCtorFn>("_ZN11QPushButtonC1EP7QWidget");
        if (!qPushButton_new) qPushButton_new = qtWidgets.getSymbol<QPushButtonCtorFn>("QPushButton");
        qPushButton_setText = qtWidgets.getSymbol<QPushButtonSetTextFn>("_ZN11QPushButton7setTextERK7QString");
        if (!qPushButton_setText) qPushButton_setText = qtWidgets.getSymbol<QPushButtonSetTextFn>("setText");
        qPushButton_text = qtWidgets.getSymbol<QPushButtonTextFn>("_ZNK11QPushButton4textEv");
        if (!qPushButton_text) qPushButton_text = qtWidgets.getSymbol<QPushButtonTextFn>("text");
        qPushButton_setIcon = qtWidgets.getSymbol<QPushButtonSetIconFn>("_ZN11QPushButton7setIconERK5QIcon");
        if (!qPushButton_setIcon) qPushButton_setIcon = qtWidgets.getSymbol<QPushButtonSetIconFn>("setIcon");
        qPushButton_setCheckable = qtWidgets.getSymbol<QPushButtonSetCheckableFn>("_ZN11QPushButton12setCheckableEb");
        if (!qPushButton_setCheckable) qPushButton_setCheckable = qtWidgets.getSymbol<QPushButtonSetCheckableFn>("setCheckable");
        qPushButton_isCheckable = qtWidgets.getSymbol<QPushButtonIsCheckableFn>("_ZNK11QPushButton11isCheckableEv");
        if (!qPushButton_isCheckable) qPushButton_isCheckable = qtWidgets.getSymbol<QPushButtonIsCheckableFn>("isCheckable");
        qPushButton_setChecked = qtWidgets.getSymbol<QPushButtonSetCheckedFn>("_ZN11QPushButton11setCheckedEb");
        if (!qPushButton_setChecked) qPushButton_setChecked = qtWidgets.getSymbol<QPushButtonSetCheckedFn>("setChecked");
        qPushButton_isChecked = qtWidgets.getSymbol<QPushButtonIsCheckedFn>("_ZNK11QPushButton9isCheckedEv");
        if (!qPushButton_isChecked) qPushButton_isChecked = qtWidgets.getSymbol<QPushButtonIsCheckedFn>("isChecked");
        qPushButton_setDefault = qtWidgets.getSymbol<QPushButtonSetDefaultFn>("_ZN11QPushButton11setDefaultEb");
        if (!qPushButton_setDefault) qPushButton_setDefault = qtWidgets.getSymbol<QPushButtonSetDefaultFn>("setDefault");
        qPushButton_setFlat = qtWidgets.getSymbol<QPushButtonSetFlatFn>("_ZN11QPushButton8setFlatEb");
        if (!qPushButton_setFlat) qPushButton_setFlat = qtWidgets.getSymbol<QPushButtonSetFlatFn>("setFlat");
        qPushButton_click = qtWidgets.getSymbol<QPushButtonClickFn>("_ZN11QPushButton5clickEv");
        if (!qPushButton_click) qPushButton_click = qtWidgets.getSymbol<QPushButtonClickFn>("click");
        qPushButton_toggle = qtWidgets.getSymbol<QPushButtonToggleFn>("_ZN11QPushButton6toggleEv");
        if (!qPushButton_toggle) qPushButton_toggle = qtWidgets.getSymbol<QPushButtonToggleFn>("toggle");
        
        /* QCheckBox */
        qCheckBox_new = qtWidgets.getSymbol<QCheckBoxCtorFn>("_ZN9QCheckBoxC1EP7QWidget");
        if (!qCheckBox_new) qCheckBox_new = qtWidgets.getSymbol<QCheckBoxCtorFn>("QCheckBox");
        qCheckBox_setText = qtWidgets.getSymbol<QCheckBoxSetTextFn>("_ZN9QCheckBox7setTextERK7QString");
        if (!qCheckBox_setText) qCheckBox_setText = qtWidgets.getSymbol<QCheckBoxSetTextFn>("setText");
        qCheckBox_text = qtWidgets.getSymbol<QCheckBoxTextFn>("_ZNK9QCheckBox4textEv");
        if (!qCheckBox_text) qCheckBox_text = qtWidgets.getSymbol<QCheckBoxTextFn>("text");
        qCheckBox_setCheckState = qtWidgets.getSymbol<QCheckBoxSetCheckStateFn>("_ZN9QCheckBox13setCheckStateEN2Qt10CheckStateE");
        if (!qCheckBox_setCheckState) qCheckBox_setCheckState = qtWidgets.getSymbol<QCheckBoxSetCheckStateFn>("setCheckState");
        qCheckBox_checkState = qtWidgets.getSymbol<QCheckBoxCheckStateFn>("_ZNK9QCheckBox9checkStateEv");
        if (!qCheckBox_checkState) qCheckBox_checkState = qtWidgets.getSymbol<QCheckBoxCheckStateFn>("checkState");
        qCheckBox_setTristate = qtWidgets.getSymbol<QCheckBoxSetTristateFn>("_ZN9QCheckBox11setTristateEb");
        if (!qCheckBox_setTristate) qCheckBox_setTristate = qtWidgets.getSymbol<QCheckBoxSetTristateFn>("setTristate");
        
        /* QRadioButton */
        qRadioButton_new = qtWidgets.getSymbol<QRadioButtonCtorFn>("_ZN12QRadioButtonC1EP7QWidget");
        if (!qRadioButton_new) qRadioButton_new = qtWidgets.getSymbol<QRadioButtonCtorFn>("QRadioButton");
        qRadioButton_setText = qtWidgets.getSymbol<QRadioButtonSetTextFn>("_ZN12QRadioButton7setTextERK7QString");
        if (!qRadioButton_setText) qRadioButton_setText = qtWidgets.getSymbol<QRadioButtonSetTextFn>("setText");
        
        /* QGroupBox */
        qGroupBox_new = qtWidgets.getSymbol<QGroupBoxCtorFn>("_ZN9QGroupBoxC1EP7QWidget");
        if (!qGroupBox_new) qGroupBox_new = qtWidgets.getSymbol<QGroupBoxCtorFn>("QGroupBox");
        qGroupBox_setTitle = qtWidgets.getSymbol<QGroupBoxSetTitleFn>("_ZN9QGroupBox8setTitleERK7QString");
        if (!qGroupBox_setTitle) qGroupBox_setTitle = qtWidgets.getSymbol<QGroupBoxSetTitleFn>("setTitle");
        qGroupBox_setCheckable = qtWidgets.getSymbol<QGroupBoxSetCheckableFn>("_ZN9QGroupBox12setCheckableEb");
        if (!qGroupBox_setCheckable) qGroupBox_setCheckable = qtWidgets.getSymbol<QGroupBoxSetCheckableFn>("setCheckable");
        
        /* QLineEdit */
        qLineEdit_new = qtWidgets.getSymbol<QLineEditCtorFn>("_ZN9QLineEditC1EP7QWidget");
        if (!qLineEdit_new) qLineEdit_new = qtWidgets.getSymbol<QLineEditCtorFn>("QLineEdit");
        qLineEdit_setText = qtWidgets.getSymbol<QLineEditSetTextFn>("_ZN9QLineEdit7setTextERK7QString");
        if (!qLineEdit_setText) qLineEdit_setText = qtWidgets.getSymbol<QLineEditSetTextFn>("setText");
        qLineEdit_text = qtWidgets.getSymbol<QLineEditTextFn>("_ZNK9QLineEdit4textEv");
        if (!qLineEdit_text) qLineEdit_text = qtWidgets.getSymbol<QLineEditTextFn>("text");
        qLineEdit_setPlaceholderText = qtWidgets.getSymbol<QLineEditSetPlaceholderTextFn>("_ZN9QLineEdit18setPlaceholderTextERK7QString");
        if (!qLineEdit_setPlaceholderText) qLineEdit_setPlaceholderText = qtWidgets.getSymbol<QLineEditSetPlaceholderTextFn>("setPlaceholderText");
        qLineEdit_placeholderText = qtWidgets.getSymbol<QLineEditPlaceholderTextFn>("_ZNK9QLineEdit14placeholderTextEv");
        if (!qLineEdit_placeholderText) qLineEdit_placeholderText = qtWidgets.getSymbol<QLineEditPlaceholderTextFn>("placeholderText");
        qLineEdit_setMaxLength = qtWidgets.getSymbol<QLineEditSetMaxLengthFn>("_ZN9QLineEdit12setMaxLengthEi");
        if (!qLineEdit_setMaxLength) qLineEdit_setMaxLength = qtWidgets.getSymbol<QLineEditSetMaxLengthFn>("setMaxLength");
        qLineEdit_setReadOnly = qtWidgets.getSymbol<QLineEditSetReadOnlyFn>("_ZN9QLineEdit11setReadOnlyEb");
        if (!qLineEdit_setReadOnly) qLineEdit_setReadOnly = qtWidgets.getSymbol<QLineEditSetReadOnlyFn>("setReadOnly");
        qLineEdit_isReadOnly = qtWidgets.getSymbol<QLineEditIsReadOnlyFn>("_ZNK9QLineEdit10isReadOnlyEv");
        if (!qLineEdit_isReadOnly) qLineEdit_isReadOnly = qtWidgets.getSymbol<QLineEditIsReadOnlyFn>("isReadOnly");
        qLineEdit_setEchoMode = qtWidgets.getSymbol<QLineEditSetEchoModeFn>("_ZN9QLineEdit11setEchoModeENS_8EchoModeE");
        if (!qLineEdit_setEchoMode) qLineEdit_setEchoMode = qtWidgets.getSymbol<QLineEditSetEchoModeFn>("setEchoMode");
        qLineEdit_setAlignment = qtWidgets.getSymbol<QLineEditSetAlignmentFn>("_ZN9QLineEdit12setAlignmentE6QFlagsIN2Qt13AlignmentFlagEE");
        if (!qLineEdit_setAlignment) qLineEdit_setAlignment = qtWidgets.getSymbol<QLineEditSetAlignmentFn>("setAlignment");
        qLineEdit_setFrame = qtWidgets.getSymbol<QLineEditSetFrameFn>("_ZN9QLineEdit8setFrameEb");
        if (!qLineEdit_setFrame) qLineEdit_setFrame = qtWidgets.getSymbol<QLineEditSetFrameFn>("setFrame");
        qLineEdit_clear = qtWidgets.getSymbol<QLineEditClearFn>("_ZN9QLineEdit5clearEv");
        if (!qLineEdit_clear) qLineEdit_clear = qtWidgets.getSymbol<QLineEditClearFn>("clear");
        qLineEdit_deselect = qtWidgets.getSymbol<QLineEditDeselectFn>("_ZN9QLineEdit8deselectEv");
        if (!qLineEdit_deselect) qLineEdit_deselect = qtWidgets.getSymbol<QLineEditDeselectFn>("deselect");
        qLineEdit_undo = qtWidgets.getSymbol<QLineEditUndoFn>("_ZN9QLineEdit4undoEv");
        if (!qLineEdit_undo) qLineEdit_undo = qtWidgets.getSymbol<QLineEditUndoFn>("undo");
        qLineEdit_redo = qtWidgets.getSymbol<QLineEditRedoFn>("_ZN9QLineEdit4redoEv");
        if (!qLineEdit_redo) qLineEdit_redo = qtWidgets.getSymbol<QLineEditRedoFn>("redo");
        
        /* QTextEdit */
        qTextEdit_new = qtWidgets.getSymbol<QTextEditCtorFn>("_ZN9QTextEditC1EP7QWidget");
        if (!qTextEdit_new) qTextEdit_new = qtWidgets.getSymbol<QTextEditCtorFn>("QTextEdit");
        qTextEdit_setPlainText = qtWidgets.getSymbol<QTextEditSetPlainTextFn>("_ZN9QTextEdit12setPlainTextERK7QString");
        if (!qTextEdit_setPlainText) qTextEdit_setPlainText = qtWidgets.getSymbol<QTextEditSetPlainTextFn>("setPlainText");
        qTextEdit_toPlainText = qtWidgets.getSymbol<QTextEditToPlainTextFn>("_ZNK9QTextEdit11toPlainTextEv");
        if (!qTextEdit_toPlainText) qTextEdit_toPlainText = qtWidgets.getSymbol<QTextEditToPlainTextFn>("toPlainText");
        qTextEdit_setHtml = qtWidgets.getSymbol<QTextEditSetHtmlFn>("_ZN9QTextEdit7setHtmlERK7QString");
        if (!qTextEdit_setHtml) qTextEdit_setHtml = qtWidgets.getSymbol<QTextEditSetHtmlFn>("setHtml");
        qTextEdit_toHtml = qtWidgets.getSymbol<QTextEditToHtmlFn>("_ZNK9QTextEdit6toHtmlEv");
        if (!qTextEdit_toHtml) qTextEdit_toHtml = qtWidgets.getSymbol<QTextEditToHtmlFn>("toHtml");
        qTextEdit_append = qtWidgets.getSymbol<QTextEditAppendFn>("_ZN9QTextEdit6appendERK7QString");
        if (!qTextEdit_append) qTextEdit_append = qtWidgets.getSymbol<QTextEditAppendFn>("append");
        qTextEdit_clear = qtWidgets.getSymbol<QTextEditClearFn>("_ZN9QTextEdit5clearEv");
        if (!qTextEdit_clear) qTextEdit_clear = qtWidgets.getSymbol<QTextEditClearFn>("clear");
        qTextEdit_setReadOnly = qtWidgets.getSymbol<QTextEditSetReadOnlyFn>("_ZN9QTextEdit11setReadOnlyEb");
        if (!qTextEdit_setReadOnly) qTextEdit_setReadOnly = qtWidgets.getSymbol<QTextEditSetReadOnlyFn>("setReadOnly");
        qTextEdit_isReadOnly = qtWidgets.getSymbol<QTextEditIsReadOnlyFn>("_ZNK9QTextEdit10isReadOnlyEv");
        if (!qTextEdit_isReadOnly) qTextEdit_isReadOnly = qtWidgets.getSymbol<QTextEditIsReadOnlyFn>("isReadOnly");
        qTextEdit_setPlaceholderText = qtWidgets.getSymbol<QTextEditSetPlaceholderTextFn>("_ZN9QTextEdit18setPlaceholderTextERK7QString");
        if (!qTextEdit_setPlaceholderText) qTextEdit_setPlaceholderText = qtWidgets.getSymbol<QTextEditSetPlaceholderTextFn>("setPlaceholderText");
        qTextEdit_copy = qtWidgets.getSymbol<QTextEditCopyFn>("_ZN9QTextEdit4copyEv");
        if (!qTextEdit_copy) qTextEdit_copy = qtWidgets.getSymbol<QTextEditCopyFn>("copy");
        qTextEdit_cut = qtWidgets.getSymbol<QTextEditCutFn>("_ZN9QTextEdit3cutEv");
        if (!qTextEdit_cut) qTextEdit_cut = qtWidgets.getSymbol<QTextEditCutFn>("cut");
        qTextEdit_paste = qtWidgets.getSymbol<QTextEditPasteFn>("_ZN9QTextEdit5pasteEv");
        if (!qTextEdit_paste) qTextEdit_paste = qtWidgets.getSymbol<QTextEditPasteFn>("paste");
        
        /* QComboBox */
        qComboBox_new = qtWidgets.getSymbol<QComboBoxCtorFn>("_ZN9QComboBoxC1EP7QWidget");
        if (!qComboBox_new) qComboBox_new = qtWidgets.getSymbol<QComboBoxCtorFn>("QComboBox");
        qComboBox_addItem = qtWidgets.getSymbol<QComboBoxAddItemFn>("_ZN9QComboBox7addItemERK7QStringRK8QVariant");
        if (!qComboBox_addItem) qComboBox_addItem = qtWidgets.getSymbol<QComboBoxAddItemFn>("addItem");
        qComboBox_insertItem = qtWidgets.getSymbol<QComboBoxInsertItemFn>("_ZN9QComboBox10insertItemEiRK7QString");
        if (!qComboBox_insertItem) qComboBox_insertItem = qtWidgets.getSymbol<QComboBoxInsertItemFn>("insertItem");
        qComboBox_removeItem = qtWidgets.getSymbol<QComboBoxRemoveItemFn>("_ZN9QComboBox10removeItemEi");
        if (!qComboBox_removeItem) qComboBox_removeItem = qtWidgets.getSymbol<QComboBoxRemoveItemFn>("removeItem");
        qComboBox_clear = qtWidgets.getSymbol<QComboBoxClearFn>("_ZN9QComboBox5clearEv");
        if (!qComboBox_clear) qComboBox_clear = qtWidgets.getSymbol<QComboBoxClearFn>("clear");
        qComboBox_count = qtWidgets.getSymbol<QComboBoxCountFn>("_ZNK9QComboBox5countEv");
        if (!qComboBox_count) qComboBox_count = qtWidgets.getSymbol<QComboBoxCountFn>("count");
        qComboBox_setCurrentIndex = qtWidgets.getSymbol<QComboBoxSetCurrentIndexFn>("_ZN9QComboBox16setCurrentIndexEi");
        if (!qComboBox_setCurrentIndex) qComboBox_setCurrentIndex = qtWidgets.getSymbol<QComboBoxSetCurrentIndexFn>("setCurrentIndex");
        qComboBox_currentIndex = qtWidgets.getSymbol<QComboBoxCurrentIndexFn>("_ZNK9QComboBox11currentIndexEv");
        if (!qComboBox_currentIndex) qComboBox_currentIndex = qtWidgets.getSymbol<QComboBoxCurrentIndexFn>("currentIndex");
        qComboBox_currentText = qtWidgets.getSymbol<QComboBoxCurrentTextFn>("_ZNK9QComboBox10currentTextEv");
        if (!qComboBox_currentText) qComboBox_currentText = qtWidgets.getSymbol<QComboBoxCurrentTextFn>("currentText");
        qComboBox_itemText = qtWidgets.getSymbol<QComboBoxItemTextFn>("_ZNK9QComboBox8itemTextEi");
        if (!qComboBox_itemText) qComboBox_itemText = qtWidgets.getSymbol<QComboBoxItemTextFn>("itemText");
        qComboBox_setEditable = qtWidgets.getSymbol<QComboBoxSetEditableFn>("_ZN9QComboBox11setEditableEb");
        if (!qComboBox_setEditable) qComboBox_setEditable = qtWidgets.getSymbol<QComboBoxSetEditableFn>("setEditable");
        qComboBox_setMaxVisibleItems = qtWidgets.getSymbol<QComboBoxSetMaxVisibleItemsFn>("_ZN9QComboBox17setMaxVisibleItemsEi");
        if (!qComboBox_setMaxVisibleItems) qComboBox_setMaxVisibleItems = qtWidgets.getSymbol<QComboBoxSetMaxVisibleItemsFn>("setMaxVisibleItems");
        
        /* QSpinBox */
        qSpinBox_new = qtWidgets.getSymbol<QSpinBoxCtorFn>("_ZN8QSpinBoxC1EP7QWidget");
        if (!qSpinBox_new) qSpinBox_new = qtWidgets.getSymbol<QSpinBoxCtorFn>("QSpinBox");
        qSpinBox_setRange = qtWidgets.getSymbol<QSpinBoxSetRangeFn>("_ZN8QSpinBox8setRangeEii");
        if (!qSpinBox_setRange) qSpinBox_setRange = qtWidgets.getSymbol<QSpinBoxSetRangeFn>("setRange");
        qSpinBox_minimum = qtWidgets.getSymbol<QSpinBoxMinimumFn>("_ZNK8QSpinBox8minimumEv");
        if (!qSpinBox_minimum) qSpinBox_minimum = qtWidgets.getSymbol<QSpinBoxMinimumFn>("minimum");
        qSpinBox_maximum = qtWidgets.getSymbol<QSpinBoxMaximumFn>("_ZNK8QSpinBox8maximumEv");
        if (!qSpinBox_maximum) qSpinBox_maximum = qtWidgets.getSymbol<QSpinBoxMaximumFn>("maximum");
        qSpinBox_setValue = qtWidgets.getSymbol<QSpinBoxSetValueFn>("_ZN8QSpinBox9setValueEi");
        if (!qSpinBox_setValue) qSpinBox_setValue = qtWidgets.getSymbol<QSpinBoxSetValueFn>("setValue");
        qSpinBox_value = qtWidgets.getSymbol<QSpinBoxValueFn>("_ZNK8QSpinBox5valueEv");
        if (!qSpinBox_value) qSpinBox_value = qtWidgets.getSymbol<QSpinBoxValueFn>("value");
        qSpinBox_setSingleStep = qtWidgets.getSymbol<QSpinBoxSetSingleStepFn>("_ZN8QSpinBox13setSingleStepEi");
        if (!qSpinBox_setSingleStep) qSpinBox_setSingleStep = qtWidgets.getSymbol<QSpinBoxSetSingleStepFn>("setSingleStep");
        qSpinBox_setPrefix = qtWidgets.getSymbol<QSpinBoxSetPrefixFn>("_ZN8QSpinBox10setPrefixERK7QString");
        if (!qSpinBox_setPrefix) qSpinBox_setPrefix = qtWidgets.getSymbol<QSpinBoxSetPrefixFn>("setPrefix");
        qSpinBox_setSuffix = qtWidgets.getSymbol<QSpinBoxSetSuffixFn>("_ZN8QSpinBox10setSuffixERK7QString");
        if (!qSpinBox_setSuffix) qSpinBox_setSuffix = qtWidgets.getSymbol<QSpinBoxSetSuffixFn>("setSuffix");
        qSpinBox_setWrapping = qtWidgets.getSymbol<QSpinBoxSetWrappingFn>("_ZN8QSpinBox11setWrappingEb");
        if (!qSpinBox_setWrapping) qSpinBox_setWrapping = qtWidgets.getSymbol<QSpinBoxSetWrappingFn>("setWrapping");
        
        /* QDoubleSpinBox */
        qDoubleSpinBox_new = qtWidgets.getSymbol<QDoubleSpinBoxCtorFn>("_ZN14QDoubleSpinBoxC1EP7QWidget");
        if (!qDoubleSpinBox_new) qDoubleSpinBox_new = qtWidgets.getSymbol<QDoubleSpinBoxCtorFn>("QDoubleSpinBox");
        qDoubleSpinBox_setRange = qtWidgets.getSymbol<QDoubleSpinBoxSetRangeFn>("_ZN14QDoubleSpinBox8setRangeEdd");
        if (!qDoubleSpinBox_setRange) qDoubleSpinBox_setRange = qtWidgets.getSymbol<QDoubleSpinBoxSetRangeFn>("setRange");
        qDoubleSpinBox_setValue = qtWidgets.getSymbol<QDoubleSpinBoxSetValueFn>("_ZN14QDoubleSpinBox9setValueEd");
        if (!qDoubleSpinBox_setValue) qDoubleSpinBox_setValue = qtWidgets.getSymbol<QDoubleSpinBoxSetValueFn>("setValue");
        qDoubleSpinBox_value = qtWidgets.getSymbol<QDoubleSpinBoxValueFn>("_ZNK14QDoubleSpinBox5valueEv");
        if (!qDoubleSpinBox_value) qDoubleSpinBox_value = qtWidgets.getSymbol<QDoubleSpinBoxValueFn>("value");
        qDoubleSpinBox_setDecimals = qtWidgets.getSymbol<QDoubleSpinBoxSetDecimalsFn>("_ZN14QDoubleSpinBox12setDecimalsEi");
        if (!qDoubleSpinBox_setDecimals) qDoubleSpinBox_setDecimals = qtWidgets.getSymbol<QDoubleSpinBoxSetDecimalsFn>("setDecimals");
        
        /* QSlider */
        qSlider_new = qtWidgets.getSymbol<QSliderCtorFn>("_ZN7QSliderC1EP7QWidget");
        if (!qSlider_new) qSlider_new = qtWidgets.getSymbol<QSliderCtorFn>("QSlider");
        qSlider_setOrientation = qtWidgets.getSymbol<QSliderSetOrientationFn>("_ZN7QSlider14setOrientationEN2Qt11OrientationE");
        if (!qSlider_setOrientation) qSlider_setOrientation = qtWidgets.getSymbol<QSliderSetOrientationFn>("setOrientation");
        qSlider_setMinimum = qtWidgets.getSymbol<QSliderSetMinimumFn>("_ZN7QSlider10setMinimumEi");
        if (!qSlider_setMinimum) qSlider_setMinimum = qtWidgets.getSymbol<QSliderSetMinimumFn>("setMinimum");
        qSlider_setMaximum = qtWidgets.getSymbol<QSliderSetMaximumFn>("_ZN7QSlider10setMaximumEi");
        if (!qSlider_setMaximum) qSlider_setMaximum = qtWidgets.getSymbol<QSliderSetMaximumFn>("setMaximum");
        qSlider_setRange = qtWidgets.getSymbol<QSliderSetRangeFn>("_ZN7QSlider8setRangeEii");
        if (!qSlider_setRange) qSlider_setRange = qtWidgets.getSymbol<QSliderSetRangeFn>("setRange");
        qSlider_setValue = qtWidgets.getSymbol<QSliderSetValueFn>("_ZN7QSlider9setValueEi");
        if (!qSlider_setValue) qSlider_setValue = qtWidgets.getSymbol<QSliderSetValueFn>("setValue");
        qSlider_value = qtWidgets.getSymbol<QSliderValueFn>("_ZNK7QSlider5valueEv");
        if (!qSlider_value) qSlider_value = qtWidgets.getSymbol<QSliderValueFn>("value");
        qSlider_setSingleStep = qtWidgets.getSymbol<QSliderSetSingleStepFn>("_ZN7QSlider13setSingleStepEi");
        if (!qSlider_setSingleStep) qSlider_setSingleStep = qtWidgets.getSymbol<QSliderSetSingleStepFn>("setSingleStep");
        qSlider_setTickPosition = qtWidgets.getSymbol<QSliderSetTickPositionFn>("_ZN7QSlider15setTickPositionENS_10TickPositionE");
        if (!qSlider_setTickPosition) qSlider_setTickPosition = qtWidgets.getSymbol<QSliderSetTickPositionFn>("setTickPosition");
        qSlider_setInvertedAppearance = qtWidgets.getSymbol<QSliderSetInvertedAppearanceFn>("_ZN7QSlider19setInvertedAppearanceEb");
        if (!qSlider_setInvertedAppearance) qSlider_setInvertedAppearance = qtWidgets.getSymbol<QSliderSetInvertedAppearanceFn>("setInvertedAppearance");
        
        /* QScrollBar */
        qScrollBar_new = qtWidgets.getSymbol<QScrollBarCtorFn>("_ZN10QScrollBarC1EP7QWidget");
        if (!qScrollBar_new) qScrollBar_new = qtWidgets.getSymbol<QScrollBarCtorFn>("QScrollBar");
        qScrollBar_setMinimum = qtWidgets.getSymbol<QScrollBarSetMinimumFn>("_ZN10QScrollBar10setMinimumEi");
        if (!qScrollBar_setMinimum) qScrollBar_setMinimum = qtWidgets.getSymbol<QScrollBarSetMinimumFn>("setMinimum");
        qScrollBar_setMaximum = qtWidgets.getSymbol<QScrollBarSetMaximumFn>("_ZN10QScrollBar10setMaximumEi");
        if (!qScrollBar_setMaximum) qScrollBar_setMaximum = qtWidgets.getSymbol<QScrollBarSetMaximumFn>("setMaximum");
        qScrollBar_setValue = qtWidgets.getSymbol<QScrollBarSetValueFn>("_ZN10QScrollBar9setValueEi");
        if (!qScrollBar_setValue) qScrollBar_setValue = qtWidgets.getSymbol<QScrollBarSetValueFn>("setValue");
        qScrollBar_value = qtWidgets.getSymbol<QScrollBarValueFn>("_ZNK10QScrollBar5valueEv");
        if (!qScrollBar_value) qScrollBar_value = qtWidgets.getSymbol<QScrollBarValueFn>("value");
        
        /* QDial */
        qDial_new = qtWidgets.getSymbol<QDialCtorFn>("_ZN5QDialC1EP7QWidget");
        if (!qDial_new) qDial_new = qtWidgets.getSymbol<QDialCtorFn>("QDial");
        qDial_setMinimum = qtWidgets.getSymbol<QDialSetMinimumFn>("_ZN5QDial10setMinimumEi");
        if (!qDial_setMinimum) qDial_setMinimum = qtWidgets.getSymbol<QDialSetMinimumFn>("setMinimum");
        qDial_setMaximum = qtWidgets.getSymbol<QDialSetMaximumFn>("_ZN5QDial10setMaximumEi");
        if (!qDial_setMaximum) qDial_setMaximum = qtWidgets.getSymbol<QDialSetMaximumFn>("setMaximum");
        qDial_setValue = qtWidgets.getSymbol<QDialSetValueFn>("_ZN5QDial9setValueEi");
        if (!qDial_setValue) qDial_setValue = qtWidgets.getSymbol<QDialSetValueFn>("setValue");
        qDial_value = qtWidgets.getSymbol<QDialValueFn>("_ZNK5QDial5valueEv");
        if (!qDial_value) qDial_value = qtWidgets.getSymbol<QDialValueFn>("value");
        qDial_setWrapping = qtWidgets.getSymbol<QDialSetWrappingFn>("_ZN5QDial11setWrappingEb");
        if (!qDial_setWrapping) qDial_setWrapping = qtWidgets.getSymbol<QDialSetWrappingFn>("setWrapping");
        qDial_setNotchesVisible = qtWidgets.getSymbol<QDialSetNotchesVisibleFn>("_ZN5QDial16setNotchesVisibleEb");
        if (!qDial_setNotchesVisible) qDial_setNotchesVisible = qtWidgets.getSymbol<QDialSetNotchesVisibleFn>("setNotchesVisible");
        
        /* QProgressBar */
        qProgressBar_new = qtWidgets.getSymbol<QProgressBarCtorFn>("_ZN12QProgressBarC1EP7QWidget");
        if (!qProgressBar_new) qProgressBar_new = qtWidgets.getSymbol<QProgressBarCtorFn>("QProgressBar");
        qProgressBar_setRange = qtWidgets.getSymbol<QProgressBarSetRangeFn>("_ZN12QProgressBar8setRangeEii");
        if (!qProgressBar_setRange) qProgressBar_setRange = qtWidgets.getSymbol<QProgressBarSetRangeFn>("setRange");
        qProgressBar_setValue = qtWidgets.getSymbol<QProgressBarSetValueFn>("_ZN12QProgressBar9setValueEi");
        if (!qProgressBar_setValue) qProgressBar_setValue = qtWidgets.getSymbol<QProgressBarSetValueFn>("setValue");
        qProgressBar_value = qtWidgets.getSymbol<QProgressBarValueFn>("_ZNK12QProgressBar5valueEv");
        if (!qProgressBar_value) qProgressBar_value = qtWidgets.getSymbol<QProgressBarValueFn>("value");
        qProgressBar_setFormat = qtWidgets.getSymbol<QProgressBarSetFormatFn>("_ZN12QProgressBar10setFormatERK7QString");
        if (!qProgressBar_setFormat) qProgressBar_setFormat = qtWidgets.getSymbol<QProgressBarSetFormatFn>("setFormat");
        qProgressBar_setOrientation = qtWidgets.getSymbol<QProgressBarSetOrientationFn>("_ZN12QProgressBar14setOrientationEN2Qt11OrientationE");
        if (!qProgressBar_setOrientation) qProgressBar_setOrientation = qtWidgets.getSymbol<QProgressBarSetOrientationFn>("setOrientation");
        qProgressBar_setTextVisible = qtWidgets.getSymbol<QProgressBarSetTextVisibleFn>("_ZN12QProgressBar14setTextVisibleEb");
        if (!qProgressBar_setTextVisible) qProgressBar_setTextVisible = qtWidgets.getSymbol<QProgressBarSetTextVisibleFn>("setTextVisible");
        qProgressBar_reset = qtWidgets.getSymbol<QProgressBarResetFn>("_ZN12QProgressBar5resetEv");
        if (!qProgressBar_reset) qProgressBar_reset = qtWidgets.getSymbol<QProgressBarResetFn>("reset");
        
        /* QFrame */
        qFrame_new = qtWidgets.getSymbol<QFrameCtorFn>("_ZN6QFrameC1EP7QWidgetNS_10ShapeE");
        if (!qFrame_new) qFrame_new = qtWidgets.getSymbol<QFrameCtorFn>("QFrame");
        qFrame_setFrameShape = qtWidgets.getSymbol<QFrameSetFrameShapeFn>("_ZN6QFrame14setFrameShapeENS_5ShapeE");
        if (!qFrame_setFrameShape) qFrame_setFrameShape = qtWidgets.getSymbol<QFrameSetFrameShapeFn>("setFrameShape");
        qFrame_setFrameShadow = qtWidgets.getSymbol<QFrameSetFrameShadowFn>("_ZN6QFrame15setFrameShadowENS_6ShadowE");
        if (!qFrame_setFrameShadow) qFrame_setFrameShadow = qtWidgets.getSymbol<QFrameSetFrameShadowFn>("setFrameShadow");
        qFrame_setLineWidth = qtWidgets.getSymbol<QFrameSetLineWidthFn>("_ZN6QFrame13setLineWidthEi");
        if (!qFrame_setLineWidth) qFrame_setLineWidth = qtWidgets.getSymbol<QFrameSetLineWidthFn>("setLineWidth");
        qFrame_setFrameStyle = qtWidgets.getSymbol<QFrameSetFrameStyleFn>("_ZN6QFrame14setFrameStyleEi");
        if (!qFrame_setFrameStyle) qFrame_setFrameStyle = qtWidgets.getSymbol<QFrameSetFrameStyleFn>("setFrameStyle");
        
        /* Layouts */
        qVBoxLayout_new = qtWidgets.getSymbol<QVBoxLayoutCtorFn>("_ZN11QVBoxLayoutC1EP7QWidget");
        if (!qVBoxLayout_new) qVBoxLayout_new = qtWidgets.getSymbol<QVBoxLayoutCtorFn>("QVBoxLayout");
        qVBoxLayout_addStretch = qtWidgets.getSymbol<QVBoxLayoutAddStretchFn>("_ZN11QVBoxLayout10addStretchEi");
        if (!qVBoxLayout_addStretch) qVBoxLayout_addStretch = qtWidgets.getSymbol<QVBoxLayoutAddStretchFn>("addStretch");
        qVBoxLayout_addSpacing = qtWidgets.getSymbol<QVBoxLayoutAddSpacingFn>("_ZN11QVBoxLayout10addSpacingEi");
        if (!qVBoxLayout_addSpacing) qVBoxLayout_addSpacing = qtWidgets.getSymbol<QVBoxLayoutAddSpacingFn>("addSpacing");
        qHBoxLayout_new = qtWidgets.getSymbol<QHBoxLayoutCtorFn>("_ZN11QHBoxLayoutC1EP7QWidget");
        if (!qHBoxLayout_new) qHBoxLayout_new = qtWidgets.getSymbol<QHBoxLayoutCtorFn>("QHBoxLayout");
        qGridLayout_new = qtWidgets.getSymbol<QGridLayoutCtorFn>("_ZN11QGridLayoutC1EP7QWidget");
        if (!qGridLayout_new) qGridLayout_new = qtWidgets.getSymbol<QGridLayoutCtorFn>("QGridLayout");
        qGridLayout_setRowSpacing = qtWidgets.getSymbol<QGridLayoutSetRowSpacingFn>("_ZN11QGridLayout13setRowSpacingEii");
        if (!qGridLayout_setRowSpacing) qGridLayout_setRowSpacing = qtWidgets.getSymbol<QGridLayoutSetRowSpacingFn>("setRowSpacing");
        qGridLayout_setColumnSpacing = qtWidgets.getSymbol<QGridLayoutSetColumnSpacingFn>("_ZN11QGridLayout16setColumnSpacingEii");
        if (!qGridLayout_setColumnSpacing) qGridLayout_setColumnSpacing = qtWidgets.getSymbol<QGridLayoutSetColumnSpacingFn>("setColumnSpacing");
        qGridLayout_setRowStretch = qtWidgets.getSymbol<QGridLayoutSetRowStretchFn>("_ZN11QGridLayout13setRowStretchEii");
        if (!qGridLayout_setRowStretch) qGridLayout_setRowStretch = qtWidgets.getSymbol<QGridLayoutSetRowStretchFn>("setRowStretch");
        qGridLayout_setColumnStretch = qtWidgets.getSymbol<QGridLayoutSetColumnStretchFn>("_ZN11QGridLayout16setColumnStretchEii");
        if (!qGridLayout_setColumnStretch) qGridLayout_setColumnStretch = qtWidgets.getSymbol<QGridLayoutSetColumnStretchFn>("setColumnStretch");
        
        /* QTabWidget */
        qTabWidget_new = qtWidgets.getSymbol<QTabWidgetCtorFn>("_ZN10QTabWidgetC1EP7QWidget");
        if (!qTabWidget_new) qTabWidget_new = qtWidgets.getSymbol<QTabWidgetCtorFn>("QTabWidget");
        qTabWidget_addTab = qtWidgets.getSymbol<QTabWidgetAddTabFn>("_ZN10QTabWidget6addTabEP7QWidgetRK7QString");
        if (!qTabWidget_addTab) qTabWidget_addTab = qtWidgets.getSymbol<QTabWidgetAddTabFn>("addTab");
        qTabWidget_insertTab = qtWidgets.getSymbol<QTabWidgetInsertTabFn>("_ZN10QTabWidget9insertTabEiP7QWidgetRK7QString");
        if (!qTabWidget_insertTab) qTabWidget_insertTab = qtWidgets.getSymbol<QTabWidgetInsertTabFn>("insertTab");
        qTabWidget_removeTab = qtWidgets.getSymbol<QTabWidgetRemoveTabFn>("_ZN10QTabWidget9removeTabEi");
        if (!qTabWidget_removeTab) qTabWidget_removeTab = qtWidgets.getSymbol<QTabWidgetRemoveTabFn>("removeTab");
        qTabWidget_setCurrentIndex = qtWidgets.getSymbol<QTabWidgetSetCurrentIndexFn>("_ZN10QTabWidget16setCurrentIndexEi");
        if (!qTabWidget_setCurrentIndex) qTabWidget_setCurrentIndex = qtWidgets.getSymbol<QTabWidgetSetCurrentIndexFn>("setCurrentIndex");
        qTabWidget_currentIndex = qtWidgets.getSymbol<QTabWidgetCurrentIndexFn>("_ZNK10QTabWidget11currentIndexEv");
        if (!qTabWidget_currentIndex) qTabWidget_currentIndex = qtWidgets.getSymbol<QTabWidgetCurrentIndexFn>("currentIndex");
        qTabWidget_currentWidget = qtWidgets.getSymbol<QTabWidgetCurrentWidgetFn>("_ZNK10QTabWidget12currentWidgetEv");
        if (!qTabWidget_currentWidget) qTabWidget_currentWidget = qtWidgets.getSymbol<QTabWidgetCurrentWidgetFn>("currentWidget");
        qTabWidget_setCurrentWidget = qtWidgets.getSymbol<QTabWidgetSetCurrentWidgetFn>("_ZN10QTabWidget17setCurrentWidgetEP7QWidget");
        if (!qTabWidget_setCurrentWidget) qTabWidget_setCurrentWidget = qtWidgets.getSymbol<QTabWidgetSetCurrentWidgetFn>("setCurrentWidget");
        qTabWidget_count = qtWidgets.getSymbol<QTabWidgetCountFn>("_ZNK10QTabWidget5countEv");
        if (!qTabWidget_count) qTabWidget_count = qtWidgets.getSymbol<QTabWidgetCountFn>("count");
        qTabWidget_setTabText = qtWidgets.getSymbol<QTabWidgetSetTabTextFn>("_ZN10QTabWidget11setTabTextEiRK7QString");
        if (!qTabWidget_setTabText) qTabWidget_setTabText = qtWidgets.getSymbol<QTabWidgetSetTabTextFn>("setTabText");
        qTabWidget_tabText = qtWidgets.getSymbol<QTabWidgetTabTextFn>("_ZNK10QTabWidget7tabTextEi");
        if (!qTabWidget_tabText) qTabWidget_tabText = qtWidgets.getSymbol<QTabWidgetTabTextFn>("tabText");
        qTabWidget_setTabEnabled = qtWidgets.getSymbol<QTabWidgetSetTabEnabledFn>("_ZN10QTabWidget14setTabEnabledEib");
        if (!qTabWidget_setTabEnabled) qTabWidget_setTabEnabled = qtWidgets.getSymbol<QTabWidgetSetTabEnabledFn>("setTabEnabled");
        qTabWidget_isTabEnabled = qtWidgets.getSymbol<QTabWidgetIsTabEnabledFn>("_ZNK10QTabWidget10isTabEnabledEi");
        if (!qTabWidget_isTabEnabled) qTabWidget_isTabEnabled = qtWidgets.getSymbol<QTabWidgetIsTabEnabledFn>("isTabEnabled");
        qTabWidget_setTabsClosable = qtWidgets.getSymbol<QTabWidgetSetTabsClosableFn>("_ZN10QTabWidget16setTabsClosableEb");
        if (!qTabWidget_setTabsClosable) qTabWidget_setTabsClosable = qtWidgets.getSymbol<QTabWidgetSetTabsClosableFn>("setTabsClosable");
        qTabWidget_setMovable = qtWidgets.getSymbol<QTabWidgetSetMovableFn>("_ZN10QTabWidget11setMovableEb");
        if (!qTabWidget_setMovable) qTabWidget_setMovable = qtWidgets.getSymbol<QTabWidgetSetMovableFn>("setMovable");
        qTabWidget_setTabPosition = qtWidgets.getSymbol<QTabWidgetSetTabPositionFn>("_ZN10QTabWidget15setTabPositionENS_10TabPositionE");
        if (!qTabWidget_setTabPosition) qTabWidget_setTabPosition = qtWidgets.getSymbol<QTabWidgetSetTabPositionFn>("setTabPosition");
        qTabWidget_clear = qtWidgets.getSymbol<QTabWidgetClearFn>("_ZN10QTabWidget5clearEv");
        if (!qTabWidget_clear) qTabWidget_clear = qtWidgets.getSymbol<QTabWidgetClearFn>("clear");
        
        /* QStackedWidget */
        qStackedWidget_new = qtWidgets.getSymbol<QStackedWidgetCtorFn>("_ZN14QStackedWidgetC1EP7QWidget");
        if (!qStackedWidget_new) qStackedWidget_new = qtWidgets.getSymbol<QStackedWidgetCtorFn>("QStackedWidget");
        qStackedWidget_addWidget = qtWidgets.getSymbol<QStackedWidgetAddWidgetFn>("_ZN14QStackedWidget10addWidgetEP7QWidget");
        if (!qStackedWidget_addWidget) qStackedWidget_addWidget = qtWidgets.getSymbol<QStackedWidgetAddWidgetFn>("addWidget");
        qStackedWidget_insertWidget = qtWidgets.getSymbol<QStackedWidgetInsertWidgetFn>("_ZN14QStackedWidget11insertWidgetEiP7QWidget");
        if (!qStackedWidget_insertWidget) qStackedWidget_insertWidget = qtWidgets.getSymbol<QStackedWidgetInsertWidgetFn>("insertWidget");
        qStackedWidget_setCurrentIndex = qtWidgets.getSymbol<QStackedWidgetSetCurrentIndexFn>("_ZN14QStackedWidget16setCurrentIndexEi");
        if (!qStackedWidget_setCurrentIndex) qStackedWidget_setCurrentIndex = qtWidgets.getSymbol<QStackedWidgetSetCurrentIndexFn>("setCurrentIndex");
        qStackedWidget_currentIndex = qtWidgets.getSymbol<QStackedWidgetCurrentIndexFn>("_ZNK14QStackedWidget11currentIndexEv");
        if (!qStackedWidget_currentIndex) qStackedWidget_currentIndex = qtWidgets.getSymbol<QStackedWidgetCurrentIndexFn>("currentIndex");
        qStackedWidget_currentWidget = qtWidgets.getSymbol<QStackedWidgetCurrentWidgetFn>("_ZNK14QStackedWidget12currentWidgetEv");
        if (!qStackedWidget_currentWidget) qStackedWidget_currentWidget = qtWidgets.getSymbol<QStackedWidgetCurrentWidgetFn>("currentWidget");
        qStackedWidget_setCurrentWidget = qtWidgets.getSymbol<QStackedWidgetSetCurrentWidgetFn>("_ZN14QStackedWidget17setCurrentWidgetEP7QWidget");
        if (!qStackedWidget_setCurrentWidget) qStackedWidget_setCurrentWidget = qtWidgets.getSymbol<QStackedWidgetSetCurrentWidgetFn>("setCurrentWidget");
        qStackedWidget_count = qtWidgets.getSymbol<QStackedWidgetCountFn>("_ZNK14QStackedWidget5countEv");
        if (!qStackedWidget_count) qStackedWidget_count = qtWidgets.getSymbol<QStackedWidgetCountFn>("count");
        qStackedWidget_widget = qtWidgets.getSymbol<QStackedWidgetWidgetFn>("_ZNK14QStackedWidget6widgetEi");
        if (!qStackedWidget_widget) qStackedWidget_widget = qtWidgets.getSymbol<QStackedWidgetWidgetFn>("widget");
        qStackedWidget_indexOf = qtWidgets.getSymbol<QStackedWidgetIndexOfFn>("_ZNK14QStackedWidget7indexOfEP7QWidget");
        if (!qStackedWidget_indexOf) qStackedWidget_indexOf = qtWidgets.getSymbol<QStackedWidgetIndexOfFn>("indexOf");
        qStackedWidget_removeWidget = qtWidgets.getSymbol<QStackedWidgetRemoveWidgetFn>("_ZN14QStackedWidget11removeWidgetEP7QWidget");
        if (!qStackedWidget_removeWidget) qStackedWidget_removeWidget = qtWidgets.getSymbol<QStackedWidgetRemoveWidgetFn>("removeWidget");
        
        /* QScrollArea */
        qScrollArea_new = qtWidgets.getSymbol<QScrollAreaCtorFn>("_ZN11QScrollAreaC1EP7QWidget");
        if (!qScrollArea_new) qScrollArea_new = qtWidgets.getSymbol<QScrollAreaCtorFn>("QScrollArea");
        qScrollArea_setWidget = qtWidgets.getSymbol<QScrollAreaSetWidgetFn>("_ZN11QScrollArea9setWidgetEP7QWidget");
        if (!qScrollArea_setWidget) qScrollArea_setWidget = qtWidgets.getSymbol<QScrollAreaSetWidgetFn>("setWidget");
        qScrollArea_widget = qtWidgets.getSymbol<QScrollAreaWidgetFn>("_ZNK11QScrollArea6widgetEv");
        if (!qScrollArea_widget) qScrollArea_widget = qtWidgets.getSymbol<QScrollAreaWidgetFn>("widget");
        qScrollArea_setWidgetResizable = qtWidgets.getSymbol<QScrollAreaSetWidgetResizableFn>("_ZN11QScrollArea19setWidgetResizableEb");
        if (!qScrollArea_setWidgetResizable) qScrollArea_setWidgetResizable = qtWidgets.getSymbol<QScrollAreaSetWidgetResizableFn>("setWidgetResizable");
        qScrollArea_setHScrollBarPolicy = qtWidgets.getSymbol<QScrollAreaSetHScrollBarPolicyFn>("_ZN11QScrollArea20setHorizontalScrollBarPolicyEN2Qt15ScrollBarPolicyE");
        if (!qScrollArea_setHScrollBarPolicy) qScrollArea_setHScrollBarPolicy = qtWidgets.getSymbol<QScrollAreaSetHScrollBarPolicyFn>("setHScrollBarPolicy");
        qScrollArea_setVScrollBarPolicy = qtWidgets.getSymbol<QScrollAreaSetVScrollBarPolicyFn>("_ZN11QScrollArea18setVerticalScrollBarPolicyEN2Qt15ScrollBarPolicyE");
        if (!qScrollArea_setVScrollBarPolicy) qScrollArea_setVScrollBarPolicy = qtWidgets.getSymbol<QScrollAreaSetVScrollBarPolicyFn>("setVScrollBarPolicy");
        
        /* QSplitter */
        qSplitter_new = qtWidgets.getSymbol<QSplitterCtorFn>("_ZN9QSplitterC1EP7QWidget");
        if (!qSplitter_new) qSplitter_new = qtWidgets.getSymbol<QSplitterCtorFn>("QSplitter");
        qSplitter_addWidget = qtWidgets.getSymbol<QSplitterAddWidgetFn>("_ZN9QSplitter9addWidgetEP7QWidget");
        if (!qSplitter_addWidget) qSplitter_addWidget = qtWidgets.getSymbol<QSplitterAddWidgetFn>("addWidget");
        qSplitter_setOrientation = qtWidgets.getSymbol<QSplitterSetOrientationFn>("_ZN9QSplitter14setOrientationEN2Qt11OrientationE");
        if (!qSplitter_setOrientation) qSplitter_setOrientation = qtWidgets.getSymbol<QSplitterSetOrientationFn>("setOrientation");
        qSplitter_setHandleWidth = qtWidgets.getSymbol<QSplitterSetHandleWidthFn>("_ZN9QSplitter14setHandleWidthEi");
        if (!qSplitter_setHandleWidth) qSplitter_setHandleWidth = qtWidgets.getSymbol<QSplitterSetHandleWidthFn>("setHandleWidth");
        
        /* QMenuBar */
        qMenuBar_new = qtWidgets.getSymbol<QMenuBarCtorFn>("_ZN8QMenuBarC1EP7QWidget");
        if (!qMenuBar_new) qMenuBar_new = qtWidgets.getSymbol<QMenuBarCtorFn>("QMenuBar");
        qMenuBar_addMenu = qtWidgets.getSymbol<QMenuBarAddMenuFn>("_ZN8QMenuBar7addMenuERK7QString");
        if (!qMenuBar_addMenu) qMenuBar_addMenu = qtWidgets.getSymbol<QMenuBarAddMenuFn>("addMenu");
        qMenuBar_addAction = qtWidgets.getSymbol<QMenuBarAddActionFn>("_ZN8QMenuBar9addActionERK7QString");
        if (!qMenuBar_addAction) qMenuBar_addAction = qtWidgets.getSymbol<QMenuBarAddActionFn>("addAction");
        
        /* QMenu */
        qMenu_new = qtWidgets.getSymbol<QMenuCtorFn>("_ZN5QMenuC1EP7QWidget");
        if (!qMenu_new) qMenu_new = qtWidgets.getSymbol<QMenuCtorFn>("QMenu");
        qMenu_addAction = qtWidgets.getSymbol<QMenuAddActionFn>("_ZN5QMenu9addActionERK7QString");
        if (!qMenu_addAction) qMenu_addAction = qtWidgets.getSymbol<QMenuAddActionFn>("addAction");
        qMenu_addSeparator = qtWidgets.getSymbol<QMenuAddSeparatorFn>("_ZN5QMenu12addSeparatorEv");
        if (!qMenu_addSeparator) qMenu_addSeparator = qtWidgets.getSymbol<QMenuAddSeparatorFn>("addSeparator");
        qMenu_clear = qtWidgets.getSymbol<QMenuClearFn>("_ZN5QMenu5clearEv");
        if (!qMenu_clear) qMenu_clear = qtWidgets.getSymbol<QMenuClearFn>("clear");
        
        /* QStatusBar */
        qStatusBar_new = qtWidgets.getSymbol<QStatusBarCtorFn>("_ZN11QStatusBarC1EP7QWidget");
        if (!qStatusBar_new) qStatusBar_new = qtWidgets.getSymbol<QStatusBarCtorFn>("QStatusBar");
        qStatusBar_showMessage = qtWidgets.getSymbol<QStatusBarShowMessageFn>("_ZN11QStatusBar11showMessageERK7QStringi");
        if (!qStatusBar_showMessage) qStatusBar_showMessage = qtWidgets.getSymbol<QStatusBarShowMessageFn>("showMessage");
        qStatusBar_clearMessage = qtWidgets.getSymbol<QStatusBarClearMessageFn>("_ZN11QStatusBar12clearMessageEv");
        if (!qStatusBar_clearMessage) qStatusBar_clearMessage = qtWidgets.getSymbol<QStatusBarClearMessageFn>("clearMessage");
        qStatusBar_addWidget = qtWidgets.getSymbol<QStatusBarAddWidgetFn>("_ZN11QStatusBar9addWidgetEP7QWidgeti");
        if (!qStatusBar_addWidget) qStatusBar_addWidget = qtWidgets.getSymbol<QStatusBarAddWidgetFn>("addWidget");
        qStatusBar_addPermanentWidget = qtWidgets.getSymbol<QStatusBarAddPermanentWidgetFn>("_ZN11QStatusBar17addPermanentWidgetEP7QWidgeti");
        if (!qStatusBar_addPermanentWidget) qStatusBar_addPermanentWidget = qtWidgets.getSymbol<QStatusBarAddPermanentWidgetFn>("addPermanentWidget");
        
        /* QMessageBox */
        qMessageBox_about = qtWidgets.getSymbol<QMessageBoxAboutFn>("_ZN11QMessageBox5aboutEP7QWidgetRK7QStringS4_");
        if (!qMessageBox_about) qMessageBox_about = qtWidgets.getSymbol<QMessageBoxAboutFn>("about");
        qMessageBox_aboutQt = qtWidgets.getSymbol<QMessageBoxAboutQtFn>("_ZN11QMessageBox7aboutQtEP7QWidgetRK7QString");
        if (!qMessageBox_aboutQt) qMessageBox_aboutQt = qtWidgets.getSymbol<QMessageBoxAboutQtFn>("aboutQt");
        qMessageBox_question = qtWidgets.getSymbol<QMessageBoxQuestionFn>("_ZN11QMessageBox8questionEP7QWidgetRK7QStringS4_NS_14StandardButtonES5_");
        if (!qMessageBox_question) qMessageBox_question = qtWidgets.getSymbol<QMessageBoxQuestionFn>("question");
        qMessageBox_information = qtWidgets.getSymbol<QMessageBoxInformationFn>("_ZN11QMessageBox11informationEP7QWidgetRK7QStringS4_NS_14StandardButtonES5_");
        if (!qMessageBox_information) qMessageBox_information = qtWidgets.getSymbol<QMessageBoxInformationFn>("information");
        qMessageBox_warning = qtWidgets.getSymbol<QMessageBoxWarningFn>("_ZN11QMessageBox7warningEP7QWidgetRK7QStringS4_NS_14StandardButtonES5_");
        if (!qMessageBox_warning) qMessageBox_warning = qtWidgets.getSymbol<QMessageBoxWarningFn>("warning");
        qMessageBox_critical = qtWidgets.getSymbol<QMessageBoxCriticalFn>("_ZN11QMessageBox7criticalEP7QWidgetRK7QStringS4_NS_14StandardButtonES5_");
        if (!qMessageBox_critical) qMessageBox_critical = qtWidgets.getSymbol<QMessageBoxCriticalFn>("critical");
        
        /* QInputDialog */
        qInputDialog_getText = qtCore.getSymbol<QInputDialogGetTextFn>("_ZN11QInputDialog7getTextEP7QWidgetRK7QStringS4_S4_6QFlagsIN2Qt10WindowTypeEEPb");
        if (!qInputDialog_getText) qInputDialog_getText = qtCore.getSymbol<QInputDialogGetTextFn>("getText");
        qInputDialog_getInteger = qtCore.getSymbol<QInputDialogGetIntegerFn>("_ZN11QInputDialog11getIntValueEP7QWidgetRK7QStringS4_Riiiiii6QFlagsIN2Qt10WindowTypeEE");
        if (!qInputDialog_getInteger) qInputDialog_getInteger = qtCore.getSymbol<QInputDialogGetIntegerFn>("getInteger");
        
        /* QFileDialog */
        qFileDialog_getOpenFileName = qtCore.getSymbol<QFileDialogGetOpenFileNameFn>("_ZN11QFileDialog16getOpenFileNameEP7QWidgetRK7QStringS4_S4_PS0_6QFlagsIN2Qt10WindowTypeEE");
        if (!qFileDialog_getOpenFileName) qFileDialog_getOpenFileName = qtCore.getSymbol<QFileDialogGetOpenFileNameFn>("getOpenFileName");
        qFileDialog_getSaveFileName = qtCore.getSymbol<QFileDialogGetSaveFileNameFn>("_ZN11QFileDialog16getSaveFileNameEP7QWidgetRK7QStringS4_S4_PS0_6QFlagsIN2Qt10WindowTypeEE");
        if (!qFileDialog_getSaveFileName) qFileDialog_getSaveFileName = qtCore.getSymbol<QFileDialogGetSaveFileNameFn>("getSaveFileName");
        qFileDialog_getExistingDirectory = qtCore.getSymbol<QFileDialogGetExistingDirectoryFn>("_ZN11QFileDialog20getExistingDirectoryEP7QWidgetRK7QStringS4_6QFlagsINS0_8OptionEE");
        if (!qFileDialog_getExistingDirectory) qFileDialog_getExistingDirectory = qtCore.getSymbol<QFileDialogGetExistingDirectoryFn>("getExistingDirectory");
        
        /* QColorDialog */
        qColorDialog_getColor = qtCore.getSymbol<QColorDialogGetColorFn>("_ZN11QColorDialog8getColorERK6QColorP7QWidgetRK7QStringNS_10ColorDialog12ColorDialogOptionsE");
        if (!qColorDialog_getColor) qColorDialog_getColor = qtCore.getSymbol<QColorDialogGetColorFn>("getColor");
        
        /* QFontDialog */
        qFontDialog_getFont = qtCore.getSymbol<QFontDialogGetFontFn>("_ZN11QFontDialog7getFontEPbP5QFontP7QWidgetRK7QString");
        if (!qFontDialog_getFont) qFontDialog_getFont = qtCore.getSymbol<QFontDialogGetFontFn>("getFont");
        
        /* QClipboard */
        /* qApp_clipboard already loaded */
        qClipboard_text = qtCore.getSymbol<QClipboardTextFn>("_ZNK10QClipboard4textENS_4ModeE");
        if (!qClipboard_text) qClipboard_text = qtCore.getSymbol<QClipboardTextFn>("text");
        qClipboard_setText = qtCore.getSymbol<QClipboardSetTextFn>("_ZN10QClipboard7setTextERK7QStringNS_4ModeE");
        if (!qClipboard_setText) qClipboard_setText = qtCore.getSymbol<QClipboardSetTextFn>("setText");
        qClipboard_clear = qtCore.getSymbol<QClipboardClearFn>("_ZN10QClipboard5clearENS_4ModeE");
        if (!qClipboard_clear) qClipboard_clear = qtCore.getSymbol<QClipboardClearFn>("clear");
        
        /* QTimer */
        qTimer_new = qtWidgets.getSymbol<QTimerCtorFn>("_ZN6QTimerC1EP7QObject");
        if (!qTimer_new) qTimer_new = qtWidgets.getSymbol<QTimerCtorFn>("QTimer");
        qTimer_start = qtWidgets.getSymbol<QTimerStartFn>("_ZN6QTimer5startEi");
        if (!qTimer_start) qTimer_start = qtWidgets.getSymbol<QTimerStartFn>("start");
        qTimer_stop = qtWidgets.getSymbol<QTimerStopFn>("_ZN6QTimer4stopEv");
        if (!qTimer_stop) qTimer_stop = qtWidgets.getSymbol<QTimerStopFn>("stop");
        qTimer_isActive = qtWidgets.getSymbol<QTimerIsActiveFn>("_ZNK6QTimer7isActiveEv");
        if (!qTimer_isActive) qTimer_isActive = qtWidgets.getSymbol<QTimerIsActiveFn>("isActive");
        qTimer_setInterval = qtWidgets.getSymbol<QTimerSetIntervalFn>("_ZN6QTimer12setIntervalEi");
        if (!qTimer_setInterval) qTimer_setInterval = qtWidgets.getSymbol<QTimerSetIntervalFn>("setInterval");
        qTimer_setSingleShot = qtWidgets.getSymbol<QTimerSetSingleShotFn>("_ZN6QTimer14setSingleShotEb");
        if (!qTimer_setSingleShot) qTimer_setSingleShot = qtWidgets.getSymbol<QTimerSetSingleShotFn>("setSingleShot");
        
        /* QSettings */
        qSettings_new = qtWidgets.getSymbol<QSettingsCtorFn>("_ZN9QSettingsC1EP7QObject");
        if (!qSettings_new) qSettings_new = qtWidgets.getSymbol<QSettingsCtorFn>("QSettings");
        qSettings_setValue = qtWidgets.getSymbol<QSettingsSetValueFn>("_ZN9QSettings8setValueERK7QStringRK8QVariant");
        if (!qSettings_setValue) qSettings_setValue = qtWidgets.getSymbol<QSettingsSetValueFn>("setValue");
        qSettings_value = qtWidgets.getSymbol<QSettingsValueFn>("_ZNK9QSettings5valueERK7QStringRK8QVariant");
        if (!qSettings_value) qSettings_value = qtWidgets.getSymbol<QSettingsValueFn>("value");
        qSettings_contains = qtWidgets.getSymbol<QSettingsContainsFn>("_ZN9QSettings8containsERK7QString");
        if (!qSettings_contains) qSettings_contains = qtWidgets.getSymbol<QSettingsContainsFn>("contains");
        qSettings_remove = qtWidgets.getSymbol<QSettingsRemoveFn>("_ZN9QSettings6removeERK7QString");
        if (!qSettings_remove) qSettings_remove = qtWidgets.getSymbol<QSettingsRemoveFn>("remove");
        qSettings_sync = qtWidgets.getSymbol<QSettingsSyncFn>("_ZN9QSettings4syncEv");
        if (!qSettings_sync) qSettings_sync = qtWidgets.getSymbol<QSettingsSyncFn>("sync");
        
        return true;
    }
    
    bool isLoaded() const {
        return qtWidgets.isLoaded();
    }
};

static Qt6Libs* g_qtLibs = nullptr;

/* Widget handle system */
std::unordered_map<int64_t, QWidget*> g_widgets;
std::unordered_map<int64_t, void*> g_menus;
std::unordered_map<int64_t, void*> g_layouts;
std::unordered_map<int64_t, void*> g_timers;
std::unordered_map<int64_t, void*> g_settings;
int64_t g_nextId = 1;

void cleanupWidget(void* ptr) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return;
    int64_t id = reinterpret_cast<int64_t>(ptr);
    auto it = g_widgets.find(id);
    if (it != g_widgets.end()) {
        g_qtLibs->qWidget_delete(it->second);
        g_widgets.erase(it);
    }
}

int64_t storeWidget(QWidget* w) {
    int64_t id = g_nextId++;
    g_widgets[id] = w;
    return id;
}

QWidget* getWidget(int64_t id) {
    auto it = g_widgets.find(id);
    return (it != g_widgets.end()) ? it->second : nullptr;
}

int64_t storeMenu(void* m) {
    int64_t id = g_nextId++;
    g_menus[id] = m;
    return id;
}

void* getMenu(int64_t id) {
    auto it = g_menus.find(id);
    return (it != g_menus.end()) ? it->second : nullptr;
}

int64_t storeLayout(void* l) {
    int64_t id = g_nextId++;
    g_layouts[id] = l;
    return id;
}

void* getLayout(int64_t id) {
    auto it = g_layouts.find(id);
    return (it != g_layouts.end()) ? it->second : nullptr;
}

/* ============================================================================
 * EVENT CALLBACK SYSTEM
 * ============================================================================ */

/* Event callback types */
enum EventType {
    EVENT_CLICKED = 0,
    EVENT_TEXT_CHANGED = 1,
    EVENT_VALUE_CHANGED = 2,
    EVENT_STATE_CHANGED = 3,
    EVENT_TIMEOUT = 4,
    EVENT_ITEM_ACTIVATED = 5,
    EVENT_TEXT_EDITED = 6,
    EVENT_RETURN_PRESSED = 7,
    EVENT_SELECTION_CHANGED = 8,
    EVENT_CURRENTIndexChanged = 9,
};

/* Callback entry for a widget event */
struct EventCallback {
    HavelValue* callback;  /* Havel function to call */
    int64_t widgetId;      /* Widget that triggered this */
    EventType type;        /* Type of event */
    
    EventCallback() : callback(nullptr), widgetId(0), type(EVENT_CLICKED) {}
    EventCallback(HavelValue* cb, int64_t id, EventType t) 
        : callback(cb), widgetId(id), type(t) {
        if (callback) havel_incref(callback);
    }
    ~EventCallback() {
        if (callback) havel_decref(callback);
    }
};

/* Map: widgetId -> list of callbacks for that widget */
std::unordered_map<int64_t, std::vector<EventCallback>> g_widgetCallbacks;

/* Map: timerId -> callback for timer timeout */
std::unordered_map<int64_t, EventCallback> g_timerCallbacks;

/* Store last known values for change detection */
std::unordered_map<int64_t, int> g_lastIntValue;
std::unordered_map<int64_t, std::string> g_lastTextValue;

/* Function to trigger a callback */
void triggerCallback(const EventCallback& cb, HavelValue* arg = nullptr) {
    if (!cb.callback) return;
    
    /* Call the Havel callback function */
    HavelValue* args[1] = { arg };
    int argc = arg ? 1 : 0;
    
    /* Get the function and call it */
    HavelValue* result = nullptr;
    /* Note: In a real implementation, you'd call the Havel VM here */
    /* For now, we just store the callback for later invocation */
    (void)result;
    (void)args;
    (void)argc;
}

/* Register a callback for a widget event */
void registerWidgetCallback(int64_t widgetId, HavelValue* callback, EventType type) {
    EventCallback cb(callback, widgetId, type);
    g_widgetCallbacks[widgetId].push_back(cb);
}

/* Register a callback for a timer */
void registerTimerCallback(int64_t timerId, HavelValue* callback) {
    EventCallback cb(callback, timerId, EVENT_TIMEOUT);
    g_timerCallbacks[timerId] = cb;
}

/* Check and trigger value change events */
void checkValueChanges() {
    for (auto& pair : g_widgetCallbacks) {
        int64_t widgetId = pair.first;
        QWidget* widget = getWidget(widgetId);
        if (!widget) continue;
        
        for (auto& cb : pair.second) {
            /* Check text changes */
            if (cb.type == EVENT_TEXT_CHANGED || cb.type == EVENT_TEXT_EDITED) {
                const char* currentText = g_qtLibs->qLineEdit_text(widget);
                std::string current(currentText ? currentText : "");
                auto it = g_lastTextValue.find(widgetId);
                if (it == g_lastTextValue.end() || it->second != current) {
                    g_lastTextValue[widgetId] = current;
                    HavelValue* arg = havel_new_string(current.c_str());
                    triggerCallback(cb, arg);
                    havel_free_value(arg);
                }
            }
            /* Check int value changes (sliders, spinbox) */
            else if (cb.type == EVENT_VALUE_CHANGED) {
                /* This would need type-specific value getters */
                /* Simplified for now */
            }
        }
    }
}

/* ============================================================================
 * CORE FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_init(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    
    if (!g_qtLibs) {
        g_qtLibs = new Qt6Libs();
        if (!g_qtLibs->load()) {
            delete g_qtLibs;
            g_qtLibs = nullptr;
            return havel_new_bool(0);
        }
    }
    
    g_qtLibs->qApp_init(nullptr, nullptr);
    return havel_new_bool(1);
}

static HavelValue* qt_exec(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    return havel_new_int(g_qtLibs->qApp_exec());
}

static HavelValue* qt_quit(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    g_qtLibs->qApp_quit();
    return havel_new_null();
}

static HavelValue* qt_processEvents(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    g_qtLibs->qApp_processEvents();
    return havel_new_null();
}

static HavelValue* qt_setApplicationName(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    const char* name = havel_get_string(argv[0]);
    g_qtLibs->qApp_setApplicationName(name);
    return havel_new_null();
}

static HavelValue* qt_setOrganizationName(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    const char* name = havel_get_string(argv[0]);
    g_qtLibs->qApp_setOrganizationName(name);
    return havel_new_null();
}

/* ============================================================================
 * WIDGET BASE FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_widget_show(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_show(w);
    return havel_new_bool(1);
}

static HavelValue* qt_widget_hide(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_hide(w);
    return havel_new_bool(1);
}

static HavelValue* qt_widget_close(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_close(w);
    return havel_new_bool(1);
}

static HavelValue* qt_widget_setVisible(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_setVisible(w, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_setEnabled(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_setEnabled(w, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_setWindowTitle(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_setWindowTitle(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_resize(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_resize(w, (int)havel_get_int(argv[1]), (int)havel_get_int(argv[2]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_move(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_move(w, (int)havel_get_int(argv[1]), (int)havel_get_int(argv[2]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_setStyleSheet(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_setStyleSheet(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_setToolTip(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_setToolTip(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_setFocus(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_setFocus(w);
    return havel_new_bool(1);
}

static HavelValue* qt_widget_update(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_update(w);
    return havel_new_bool(1);
}

/* ============================================================================
 * MAIN WINDOW
 * ============================================================================ */

static HavelValue* qt_mainWindow_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    QMainWindow* mw = g_qtLibs->qMainWindow_new(parent);
    return havel_new_handle(reinterpret_cast<void*>(storeWidget(mw)), cleanupWidget);
}

static HavelValue* qt_mainWindow_setCentralWidget(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QMainWindow* mw = reinterpret_cast<QMainWindow*>(getWidget(id));
    if (!mw) return havel_new_bool(0);
    QWidget* cw = getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[1])));
    if (!cw) return havel_new_bool(0);
    g_qtLibs->qMainWindow_setCentralWidget(mw, cw);
    return havel_new_bool(1);
}

/* ============================================================================
 * LABEL
 * ============================================================================ */

static HavelValue* qt_label_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    const char* text = (argc >= 1) ? havel_get_string(argv[0]) : "";
    QWidget* parent = (argc >= 2) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[1]))) : nullptr;
    QWidget* label = g_qtLibs->qLabel_new(text, parent);
    return havel_new_handle(reinterpret_cast<void*>(storeWidget(label)), cleanupWidget);
}

static HavelValue* qt_label_setText(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qLabel_setText(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_label_setAlignment(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qLabel_setAlignment(w, (int)havel_get_int(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_label_setWordWrap(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qLabel_setWordWrap(w, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

/* ============================================================================
 * PUSH BUTTON
 * ============================================================================ */

static HavelValue* qt_pushButton_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    const char* text = (argc >= 1) ? havel_get_string(argv[0]) : "";
    QWidget* parent = (argc >= 2) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[1]))) : nullptr;
    QWidget* btn = g_qtLibs->qPushButton_new(text, parent);
    return havel_new_handle(reinterpret_cast<void*>(storeWidget(btn)), cleanupWidget);
}

static HavelValue* qt_pushButton_setText(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qPushButton_setText(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_pushButton_setCheckable(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qPushButton_setCheckable(w, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_pushButton_setChecked(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qPushButton_setChecked(w, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_pushButton_click(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_null();
    g_qtLibs->qPushButton_click(w);
    return havel_new_null();
}

/* ============================================================================
 * LINE EDIT
 * ============================================================================ */

static HavelValue* qt_lineEdit_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    const char* text = (argc >= 1) ? havel_get_string(argv[0]) : "";
    QWidget* parent = (argc >= 2) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[1]))) : nullptr;
    QWidget* le = g_qtLibs->qLineEdit_new(text, parent);
    return havel_new_handle(reinterpret_cast<void*>(storeWidget(le)), cleanupWidget);
}

static HavelValue* qt_lineEdit_setText(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qLineEdit_setText(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_lineEdit_text(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_string("");
    const char* text = g_qtLibs->qLineEdit_text(w);
    return havel_new_string(text ? text : "");
}

static HavelValue* qt_lineEdit_setPlaceholderText(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qLineEdit_setPlaceholderText(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_lineEdit_setReadOnly(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qLineEdit_setReadOnly(w, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_lineEdit_clear(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_null();
    g_qtLibs->qLineEdit_clear(w);
    return havel_new_null();
}

/* ============================================================================
 * LAYOUTS
 * ============================================================================ */

static HavelValue* qt_vBoxLayout_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    void* layout = g_qtLibs->qVBoxLayout_new(parent);
    return havel_new_handle(reinterpret_cast<void*>(storeLayout(layout)), nullptr);
}

static HavelValue* qt_hBoxLayout_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    void* layout = g_qtLibs->qHBoxLayout_new(parent);
    return havel_new_handle(reinterpret_cast<void*>(storeLayout(layout)), nullptr);
}

static HavelValue* qt_layout_addWidget(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t layoutId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    void* layout = getLayout(layoutId);
    QWidget* w = getWidget(widgetId);
    if (!layout || !w) return havel_new_bool(0);
    g_qtLibs->qLayout_addWidget(layout, w);
    return havel_new_bool(1);
}

static HavelValue* qt_layout_addStretch(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    int64_t layoutId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    void* layout = getLayout(layoutId);
    if (!layout) return havel_new_null();
    int stretch = (argc >= 2) ? (int)havel_get_int(argv[1]) : 1;
    g_qtLibs->qVBoxLayout_addStretch(layout, stretch);
    return havel_new_null();
}

static HavelValue* qt_widget_setLayout(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t layoutId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    QWidget* w = getWidget(widgetId);
    void* layout = getLayout(layoutId);
    if (!w || !layout) return havel_new_bool(0);
    g_qtLibs->qWidget_setLayout(w, layout);
    return havel_new_bool(1);
}

/* ============================================================================
 * MESSAGE BOX
 * ============================================================================ */

static HavelValue* qt_messageBox_about(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* title = (argc >= 2) ? havel_get_string(argv[1]) : "";
    const char* text = (argc >= 3) ? havel_get_string(argv[2]) : "";
    g_qtLibs->qMessageBox_about(parent, title, text);
    return havel_new_null();
}

static HavelValue* qt_messageBox_information(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* title = havel_get_string(argv[1]);
    const char* text = havel_get_string(argv[2]);
    int buttons = (argc >= 4) ? (int)havel_get_int(argv[3]) : Qt_Ok;
    int defaultBtn = (argc >= 5) ? (int)havel_get_int(argv[4]) : Qt_Ok;
    return havel_new_int(g_qtLibs->qMessageBox_information(parent, title, text, buttons, defaultBtn));
}

static HavelValue* qt_messageBox_question(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* title = havel_get_string(argv[1]);
    const char* text = havel_get_string(argv[2]);
    int buttons = (argc >= 4) ? (int)havel_get_int(argv[3]) : Qt_Yes | Qt_No;
    int defaultBtn = (argc >= 5) ? (int)havel_get_int(argv[4]) : Qt_No;
    return havel_new_int(g_qtLibs->qMessageBox_question(parent, title, text, buttons, defaultBtn));
}

static HavelValue* qt_messageBox_warning(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* title = havel_get_string(argv[1]);
    const char* text = havel_get_string(argv[2]);
    int buttons = (argc >= 4) ? (int)havel_get_int(argv[3]) : Qt_Ok;
    int defaultBtn = (argc >= 5) ? (int)havel_get_int(argv[4]) : Qt_Ok;
    return havel_new_int(g_qtLibs->qMessageBox_warning(parent, title, text, buttons, defaultBtn));
}

/* ============================================================================
 * TIMER FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_timer_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    void* timer = g_qtLibs->qTimer_new(nullptr);
    int64_t id = g_nextId++;
    g_timers[id] = timer;
    return havel_new_handle(reinterpret_cast<void*>(id), nullptr);
}

static HavelValue* qt_timer_start(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_timers.find(id);
    if (it == g_timers.end()) return havel_new_bool(0);
    int msec = (int)havel_get_int(argv[1]);
    g_qtLibs->qTimer_start(it->second, msec);
    return havel_new_bool(1);
}

static HavelValue* qt_timer_stop(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_timers.find(id);
    if (it == g_timers.end()) return havel_new_bool(0);
    g_qtLibs->qTimer_stop(it->second);
    return havel_new_bool(1);
}

static HavelValue* qt_timer_setInterval(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_timers.find(id);
    if (it == g_timers.end()) return havel_new_bool(0);
    int msec = (int)havel_get_int(argv[1]);
    g_qtLibs->qTimer_setInterval(it->second, msec);
    return havel_new_bool(1);
}

static HavelValue* qt_timer_setSingleShot(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_timers.find(id);
    if (it == g_timers.end()) return havel_new_bool(0);
    g_qtLibs->qTimer_setSingleShot(it->second, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_timer_isActive(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_timers.find(id);
    if (it == g_timers.end()) return havel_new_bool(0);
    return havel_new_bool(g_qtLibs->qTimer_isActive(it->second));
}

/* ============================================================================
 * SETTINGS FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_settings_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    const char* fileName = (argc >= 1) ? havel_get_string(argv[0]) : "settings.ini";
    void* settings = g_qtLibs->qSettings_new(fileName, Qt_IniFormat, nullptr);
    int64_t id = g_nextId++;
    g_settings[id] = settings;
    return havel_new_handle(reinterpret_cast<void*>(id), nullptr);
}

static HavelValue* qt_settings_setValue(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_settings.find(id);
    if (it == g_settings.end()) return havel_new_bool(0);
    const char* key = havel_get_string(argv[1]);
    const char* val = havel_get_string(argv[2]);
    g_qtLibs->qSettings_setValue(it->second, key, val);
    return havel_new_bool(1);
}

static HavelValue* qt_settings_value(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_settings.find(id);
    if (it == g_settings.end()) return havel_new_string("");
    const char* key = havel_get_string(argv[1]);
    const char* defVal = (argc >= 3) ? havel_get_string(argv[2]) : "";
    const char* val = g_qtLibs->qSettings_value(it->second, key, defVal);
    return havel_new_string(val ? val : "");
}

static HavelValue* qt_settings_contains(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_settings.find(id);
    if (it == g_settings.end()) return havel_new_bool(0);
    const char* key = havel_get_string(argv[1]);
    return havel_new_bool(g_qtLibs->qSettings_contains(it->second, key));
}

static HavelValue* qt_settings_remove(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_settings.find(id);
    if (it == g_settings.end()) return havel_new_bool(0);
    const char* key = havel_get_string(argv[1]);
    g_qtLibs->qSettings_remove(it->second, key);
    return havel_new_bool(1);
}

static HavelValue* qt_settings_sync(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_settings.find(id);
    if (it == g_settings.end()) return havel_new_bool(0);
    g_qtLibs->qSettings_sync(it->second);
    return havel_new_bool(1);
}

/* ============================================================================
 * CLIPBOARD FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_clipboard_text(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    void* cb = g_qtLibs->qApp_clipboard();
    if (!cb) return havel_new_string("");
    const char* text = g_qtLibs->qClipboard_text(cb, 0);
    return havel_new_string(text ? text : "");
}

static HavelValue* qt_clipboard_setText(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    void* cb = g_qtLibs->qApp_clipboard();
    if (!cb) return havel_new_bool(0);
    const char* text = havel_get_string(argv[0]);
    g_qtLibs->qClipboard_setText(cb, text, 0);
    return havel_new_bool(1);
}

static HavelValue* qt_clipboard_clear(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    void* cb = g_qtLibs->qApp_clipboard();
    if (!cb) return havel_new_bool(0);
    g_qtLibs->qClipboard_clear(cb);
    return havel_new_bool(1);
}

/* ============================================================================
 * FILE DIALOG FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_fileDialog_getOpenFileName(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* caption = (argc >= 2) ? havel_get_string(argv[1]) : "Open File";
    const char* dir = (argc >= 3) ? havel_get_string(argv[2]) : "";
    const char* filter = (argc >= 4) ? havel_get_string(argv[3]) : "All Files (*)";
    const char* fileName = g_qtLibs->qFileDialog_getOpenFileName(parent, caption, dir, filter, nullptr, 0);
    return havel_new_string(fileName ? fileName : "");
}

static HavelValue* qt_fileDialog_getSaveFileName(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* caption = (argc >= 2) ? havel_get_string(argv[1]) : "Save File";
    const char* dir = (argc >= 3) ? havel_get_string(argv[2]) : "";
    const char* filter = (argc >= 4) ? havel_get_string(argv[3]) : "All Files (*)";
    const char* fileName = g_qtLibs->qFileDialog_getSaveFileName(parent, caption, dir, filter, nullptr, 0);
    return havel_new_string(fileName ? fileName : "");
}

static HavelValue* qt_fileDialog_getExistingDirectory(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* caption = (argc >= 2) ? havel_get_string(argv[1]) : "Select Directory";
    const char* dir = (argc >= 3) ? havel_get_string(argv[2]) : "";
    const char* path = g_qtLibs->qFileDialog_getExistingDirectory(parent, caption, dir, 0);
    return havel_new_string(path ? path : "");
}

/* ============================================================================
 * COLOR DIALOG FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_colorDialog_getColor(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    const char* initial = (argc >= 1) ? havel_get_string(argv[0]) : "#ffffff";
    QWidget* parent = (argc >= 2) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[1]))) : nullptr;
    const char* color = g_qtLibs->qColorDialog_getColor(nullptr, initial, parent, "Select Color", 0);
    return havel_new_string(color ? color : "");
}

/* ============================================================================
 * FONT DIALOG FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_fontDialog_getFont(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* font = g_qtLibs->qFontDialog_getFont(nullptr, nullptr, parent, "Select Font", 0);
    return havel_new_string(font ? font : "");
}

/* ============================================================================
 * INPUT DIALOG FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_inputDialog_getText(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* title = havel_get_string(argv[1]);
    const char* label = havel_get_string(argv[2]);
    const char* text = (argc >= 4) ? havel_get_string(argv[3]) : "";
    bool ok = false;
    const char* result = g_qtLibs->qInputDialog_getText(parent, title, label, Qt_NormalEcho, text, &ok, 0);
    return havel_new_string(ok ? (result ? result : "") : "");
}

static HavelValue* qt_inputDialog_getInteger(int argc, HavelValue** argv) {
    if (argc < 4 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* title = havel_get_string(argv[1]);
    const char* label = havel_get_string(argv[2]);
    int value = (int)havel_get_int(argv[3]);
    int min = (argc >= 5) ? (int)havel_get_int(argv[4]) : 0;
    int max = (argc >= 6) ? (int)havel_get_int(argv[5]) : 100;
    int step = (argc >= 7) ? (int)havel_get_int(argv[6]) : 1;
    bool ok = false;
    int result = g_qtLibs->qInputDialog_getInteger(parent, title, label, &ok, value, min, max, step, 0);
    return ok ? havel_new_int(result) : havel_new_int(0);
}

/* ============================================================================
 * EVENT CONNECTION FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_onClicked(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    
    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];
    
    QWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);
    
    registerWidgetCallback(widgetId, callback, EVENT_CLICKED);
    return havel_new_bool(1);
}

static HavelValue* qt_onTextChanged(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    
    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];
    
    QWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);
    
    /* Store initial text value */
    const char* text = g_qtLibs->qLineEdit_text(widget);
    if (text) g_lastTextValue[widgetId] = text;
    
    registerWidgetCallback(widgetId, callback, EVENT_TEXT_CHANGED);
    return havel_new_bool(1);
}

static HavelValue* qt_onValueChanged(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    
    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];
    
    QWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);
    
    registerWidgetCallback(widgetId, callback, EVENT_VALUE_CHANGED);
    return havel_new_bool(1);
}

static HavelValue* qt_onStateChanged(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    
    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];
    
    QWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);
    
    registerWidgetCallback(widgetId, callback, EVENT_STATE_CHANGED);
    return havel_new_bool(1);
}

static HavelValue* qt_onTimerTimeout(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    
    int64_t timerId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];
    
    auto it = g_timers.find(timerId);
    if (it == g_timers.end()) return havel_new_bool(0);
    
    registerTimerCallback(timerId, callback);
    return havel_new_bool(1);
}

static HavelValue* qt_onReturnPressed(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    
    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];
    
    QWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);
    
    registerWidgetCallback(widgetId, callback, EVENT_RETURN_PRESSED);
    return havel_new_bool(1);
}

static HavelValue* qt_onItemActivated(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    
    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];
    
    QWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);
    
    registerWidgetCallback(widgetId, callback, EVENT_ITEM_ACTIVATED);
    return havel_new_bool(1);
}

static HavelValue* qt_onCurrentIndexChanged(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    
    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];
    
    QWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);
    
    registerWidgetCallback(widgetId, callback, EVENT_CURRENTIndexChanged);
    return havel_new_bool(1);
}

static HavelValue* qt_processCallbacks(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    checkValueChanges();
    return havel_new_null();
}

/* ============================================================================
 * CANVAS API - QuickDraw Style
 * ============================================================================ */

#include <QPainter>
#include <QPixmap>
#include <QStack>

struct QtCanvas {
    int64_t id;
    QPixmap* display;      // What you see
    QPixmap* working;      // Offscreen buffer for drawing
    QPixmap* snapshot;     // For undo
    int width;
    int height;
    QColor penColor;
    int penWidth;
    
    QtCanvas(int w, int h) : width(w), height(h), penColor(Qt::black), penWidth(1) {
        display = new QPixmap(w, h);
        working = new QPixmap(w, h);
        snapshot = new QPixmap(w, h);
        display->fill(Qt::white);
        working->fill(Qt::white);
        snapshot->fill(Qt::white);
    }
    
    ~QtCanvas() {
        delete display;
        delete working;
        delete snapshot;
    }
    
    void saveSnapshot() {
        *snapshot = *working;
    }
    
    void restoreSnapshot() {
        *working = *snapshot;
    }
    
    void commitToDisplay() {
        *display = *working;
    }
};

static std::unordered_map<int64_t, QtCanvas*> g_canvases;
static int64_t g_nextCanvasId = 1;

static QtCanvas* getCanvas(int64_t id) {
    auto it = g_canvases.find(id);
    if (it != g_canvases.end()) return it->second;
    return nullptr;
}

static HavelValue* qt_canvasNew(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_null();
    
    int width = static_cast<int>(havel_get_int(argv[0]));
    int height = static_cast<int>(havel_get_int(argv[1]));
    
    QtCanvas* canvas = new QtCanvas(width, height);
    canvas->id = g_nextCanvasId++;
    g_canvases[canvas->id] = canvas;
    
    return havel_new_handle(reinterpret_cast<void*>(canvas->id), 0);
}

static HavelValue* qt_canvasClear(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    
    int64_t canvasId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QtCanvas* canvas = getCanvas(canvasId);
    if (!canvas) return havel_new_bool(0);
    
    canvas->working->fill(Qt::white);
    canvas->commitToDisplay();
    return havel_new_bool(1);
}

static HavelValue* qt_canvasSetPen(int argc, HavelValue** argv) {
    if (argc < 3) return havel_new_bool(0);
    
    int64_t canvasId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QtCanvas* canvas = getCanvas(canvasId);
    if (!canvas) return havel_new_bool(0);
    
    int r = static_cast<int>(havel_get_int(argv[1]));
    int g = static_cast<int>(havel_get_int(argv[2]));
    int b = static_cast<int>(argc > 3 ? havel_get_int(argv[3]) : 0);
    
    canvas->penColor = QColor(r, g, b);
    if (argc > 4) {
        canvas->penWidth = static_cast<int>(havel_get_int(argv[4]));
    }
    
    return havel_new_bool(1);
}

static HavelValue* qt_canvasDrawLine(int argc, HavelValue** argv) {
    if (argc < 5) return havel_new_bool(0);
    
    int64_t canvasId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QtCanvas* canvas = getCanvas(canvasId);
    if (!canvas) return havel_new_bool(0);
    
    int x1 = static_cast<int>(havel_get_int(argv[1]));
    int y1 = static_cast<int>(havel_get_int(argv[2]));
    int x2 = static_cast<int>(havel_get_int(argv[3]));
    int y2 = static_cast<int>(havel_get_int(argv[4]));
    
    QPainter painter(canvas->working);
    painter.setPen(QPen(canvas->penColor, canvas->penWidth));
    painter.drawLine(x1, y1, x2, y2);
    painter.end();
    
    canvas->commitToDisplay();
    return havel_new_bool(1);
}

static HavelValue* qt_canvasDrawRect(int argc, HavelValue** argv) {
    if (argc < 5) return havel_new_bool(0);
    
    int64_t canvasId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QtCanvas* canvas = getCanvas(canvasId);
    if (!canvas) return havel_new_bool(0);
    
    int x = static_cast<int>(havel_get_int(argv[1]));
    int y = static_cast<int>(havel_get_int(argv[2]));
    int w = static_cast<int>(havel_get_int(argv[3]));
    int h = static_cast<int>(havel_get_int(argv[4]));
    
    QPainter painter(canvas->working);
    painter.setPen(QPen(canvas->penColor, canvas->penWidth));
    if (argc > 5 && havel_get_bool(argv[5])) {
        painter.fillRect(x, y, w, h, canvas->penColor);
    } else {
        painter.drawRect(x, y, w, h);
    }
    painter.end();
    
    canvas->commitToDisplay();
    return havel_new_bool(1);
}

static HavelValue* qt_canvasDrawCircle(int argc, HavelValue** argv) {
    if (argc < 4) return havel_new_bool(0);
    
    int64_t canvasId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QtCanvas* canvas = getCanvas(canvasId);
    if (!canvas) return havel_new_bool(0);
    
    int x = static_cast<int>(havel_get_int(argv[1]));
    int y = static_cast<int>(havel_get_int(argv[2]));
    int r = static_cast<int>(havel_get_int(argv[3]));
    
    QPainter painter(canvas->working);
    painter.setPen(QPen(canvas->penColor, canvas->penWidth));
    if (argc > 4 && havel_get_bool(argv[4])) {
        painter.setBrush(canvas->penColor);
    }
    painter.drawEllipse(x - r, y - r, r * 2, r * 2);
    painter.end();
    
    canvas->commitToDisplay();
    return havel_new_bool(1);
}

static void floodFill(QPixmap* pixmap, int startX, int startY, const QColor& fillColor) {
    QImage image = pixmap->toImage();
    if (startX < 0 || startX >= image.width() || startY < 0 || startY >= image.height())
        return;
    
    QRgb targetColor = image.pixel(startX, startY);
    QRgb fillRgb = fillColor.rgb();
    
    if (targetColor == fillRgb) return;
    
    QStack<QPoint> stack;
    stack.push(QPoint(startX, startY));
    
    while (!stack.isEmpty()) {
        QPoint p = stack.pop();
        int x = p.x();
        int y = p.y();
        
        if (x < 0 || x >= image.width() || y < 0 || y >= image.height())
            continue;
        if (image.pixel(x, y) != targetColor)
            continue;
        
        image.setPixel(x, y, fillRgb);
        
        stack.push(QPoint(x + 1, y));
        stack.push(QPoint(x - 1, y));
        stack.push(QPoint(x, y + 1));
        stack.push(QPoint(x, y - 1));
    }
    
    *pixmap = QPixmap::fromImage(image);
}

static HavelValue* qt_canvasFill(int argc, HavelValue** argv) {
    if (argc < 3) return havel_new_bool(0);
    
    int64_t canvasId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QtCanvas* canvas = getCanvas(canvasId);
    if (!canvas) return havel_new_bool(0);
    
    int x = static_cast<int>(havel_get_int(argv[1]));
    int y = static_cast<int>(havel_get_int(argv[2]));
    
    floodFill(canvas->working, x, y, canvas->penColor);
    canvas->commitToDisplay();
    
    return havel_new_bool(1);
}

static HavelValue* qt_canvasBeginStroke(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    
    int64_t canvasId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QtCanvas* canvas = getCanvas(canvasId);
    if (!canvas) return havel_new_bool(0);
    
    // Save current state for undo
    canvas->saveSnapshot();
    return havel_new_bool(1);
}

static HavelValue* qt_canvasEndStroke(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    
    int64_t canvasId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QtCanvas* canvas = getCanvas(canvasId);
    if (!canvas) return havel_new_bool(0);
    
    canvas->commitToDisplay();
    return havel_new_bool(1);
}

static HavelValue* qt_canvasUndo(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    
    int64_t canvasId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QtCanvas* canvas = getCanvas(canvasId);
    if (!canvas) return havel_new_bool(0);
    
    canvas->restoreSnapshot();
    canvas->commitToDisplay();
    return havel_new_bool(1);
}

static HavelValue* qt_canvasGetImage(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_null();
    
    int64_t canvasId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QtCanvas* canvas = getCanvas(canvasId);
    if (!canvas) return havel_new_null();
    
    // Convert to QLabel for display
    QLabel* label = new QLabel();
    label->setPixmap(*canvas->display);
    label->setFixedSize(canvas->width, canvas->height);
    label->show();
    
    int64_t widgetId = reinterpret_cast<int64_t>(label);
    g_widgets[widgetId] = label;
    
    return havel_new_handle(reinterpret_cast<void*>(widgetId), 0);
}

static HavelValue* qt_canvasLassoSelect(int argc, HavelValue** argv) {
    if (argc < 3) return havel_new_null();
    
    int64_t canvasId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QtCanvas* canvas = getCanvas(canvasId);
    if (!canvas) return havel_new_null();
    
    int startX = static_cast<int>(havel_get_int(argv[1]));
    int startY = static_cast<int>(havel_get_int(argv[2]));
    
    // Use flood fill to find connected region (lasso)
    QImage image = canvas->working->toImage();
    if (startX < 0 || startX >= image.width() || startY < 0 || startY >= image.height())
        return havel_new_null();
    
    QRgb targetColor = image.pixel(startX, startY);
    QColor highlightColor(255, 0, 0, 128); // Semi-transparent red
    
    // Create mask of selected region using flood fill logic
    QStack<QPoint> stack;
    QImage mask(image.width(), image.height(), QImage::Format_ARGB32);
    mask.fill(Qt::transparent);
    
    stack.push(QPoint(startX, startY));
    while (!stack.isEmpty()) {
        QPoint p = stack.pop();
        int x = p.x();
        int y = p.y();
        
        if (x < 0 || x >= image.width() || y < 0 || y >= image.height())
            continue;
        if (mask.pixel(x, y) != QColor(Qt::transparent).rgba())
            continue;
        if (image.pixel(x, y) != targetColor)
            continue;
        
        mask.setPixel(x, y, highlightColor.rgba());
        
        stack.push(QPoint(x + 1, y));
        stack.push(QPoint(x - 1, y));
        stack.push(QPoint(x, y + 1));
        stack.push(QPoint(x, y - 1));
    }
    
    // Return bounds of selection as [x, y, w, h]
    int minX = image.width(), minY = image.height();
    int maxX = 0, maxY = 0;
    
    for (int y = 0; y < mask.height(); y++) {
        for (int x = 0; x < mask.width(); x++) {
            if (mask.pixel(x, y) == highlightColor.rgba()) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    
    if (maxX < minX) return havel_new_null();
    
    // Return array [x, y, width, height]
    HavelValue* arr = havel_new_array(4);
    havel_array_set(arr, 0, havel_new_int(minX));
    havel_array_set(arr, 1, havel_new_int(minY));
    havel_array_set(arr, 2, havel_new_int(maxX - minX + 1));
    havel_array_set(arr, 3, havel_new_int(maxY - minY + 1));
    
    return arr;
}

/* ============================================================================
 * REGISTRATION
 * ============================================================================ */

} /* anonymous namespace */

extern "C" void havel_extension_init(HavelAPI* api) {
    /* Core */
    api->register_function("qt", "init", qt_init);
    api->register_function("qt", "exec", qt_exec);
    api->register_function("qt", "quit", qt_quit);
    api->register_function("qt", "processEvents", qt_processEvents);
    api->register_function("qt", "setApplicationName", qt_setApplicationName);
    api->register_function("qt", "setOrganizationName", qt_setOrganizationName);

    /* Widget base */
    api->register_function("qt", "widgetShow", qt_widget_show);
    api->register_function("qt", "widgetHide", qt_widget_hide);
    api->register_function("qt", "widgetClose", qt_widget_close);
    api->register_function("qt", "widgetSetVisible", qt_widget_setVisible);
    api->register_function("qt", "widgetSetEnabled", qt_widget_setEnabled);
    api->register_function("qt", "widgetSetWindowTitle", qt_widget_setWindowTitle);
    api->register_function("qt", "widgetResize", qt_widget_resize);
    api->register_function("qt", "widgetMove", qt_widget_move);
    api->register_function("qt", "widgetSetStyleSheet", qt_widget_setStyleSheet);
    api->register_function("qt", "widgetSetToolTip", qt_widget_setToolTip);
    api->register_function("qt", "widgetSetFocus", qt_widget_setFocus);
    api->register_function("qt", "widgetUpdate", qt_widget_update);

    /* QMainWindow */
    api->register_function("qt", "mainWindowNew", qt_mainWindow_new);
    api->register_function("qt", "mainWindowSetCentralWidget", qt_mainWindow_setCentralWidget);

    /* QLabel */
    api->register_function("qt", "labelNew", qt_label_new);
    api->register_function("qt", "labelSetText", qt_label_setText);
    api->register_function("qt", "labelSetAlignment", qt_label_setAlignment);
    api->register_function("qt", "labelSetWordWrap", qt_label_setWordWrap);

    /* QPushButton */
    api->register_function("qt", "pushButtonNew", qt_pushButton_new);
    api->register_function("qt", "pushButtonSetText", qt_pushButton_setText);
    api->register_function("qt", "pushButtonSetCheckable", qt_pushButton_setCheckable);
    api->register_function("qt", "pushButtonSetChecked", qt_pushButton_setChecked);
    api->register_function("qt", "pushButtonClick", qt_pushButton_click);

    /* QLineEdit */
    api->register_function("qt", "lineEditNew", qt_lineEdit_new);
    api->register_function("qt", "lineEditSetText", qt_lineEdit_setText);
    api->register_function("qt", "lineEditText", qt_lineEdit_text);
    api->register_function("qt", "lineEditSetPlaceholderText", qt_lineEdit_setPlaceholderText);
    api->register_function("qt", "lineEditSetReadOnly", qt_lineEdit_setReadOnly);
    api->register_function("qt", "lineEditClear", qt_lineEdit_clear);

    /* Layouts */
    api->register_function("qt", "vBoxLayoutNew", qt_vBoxLayout_new);
    api->register_function("qt", "hBoxLayoutNew", qt_hBoxLayout_new);
    api->register_function("qt", "layoutAddWidget", qt_layout_addWidget);
    api->register_function("qt", "layoutAddStretch", qt_layout_addStretch);
    api->register_function("qt", "widgetSetLayout", qt_widget_setLayout);

    /* QMessageBox */
    api->register_function("qt", "messageBoxAbout", qt_messageBox_about);
    api->register_function("qt", "messageBoxInformation", qt_messageBox_information);
    api->register_function("qt", "messageBoxQuestion", qt_messageBox_question);
    api->register_function("qt", "messageBoxWarning", qt_messageBox_warning);

    /* QTimer */
    api->register_function("qt", "timerNew", qt_timer_new);
    api->register_function("qt", "timerStart", qt_timer_start);
    api->register_function("qt", "timerStop", qt_timer_stop);
    api->register_function("qt", "timerSetInterval", qt_timer_setInterval);
    api->register_function("qt", "timerSetSingleShot", qt_timer_setSingleShot);
    api->register_function("qt", "timerIsActive", qt_timer_isActive);

    /* QSettings */
    api->register_function("qt", "settingsNew", qt_settings_new);
    api->register_function("qt", "settingsSetValue", qt_settings_setValue);
    api->register_function("qt", "settingsValue", qt_settings_value);
    api->register_function("qt", "settingsContains", qt_settings_contains);
    api->register_function("qt", "settingsRemove", qt_settings_remove);
    api->register_function("qt", "settingsSync", qt_settings_sync);

    /* QClipboard */
    api->register_function("qt", "clipboardText", qt_clipboard_text);
    api->register_function("qt", "clipboardSetText", qt_clipboard_setText);
    api->register_function("qt", "clipboardClear", qt_clipboard_clear);

    /* QFileDialog */
    api->register_function("qt", "fileDialogGetOpenFileName", qt_fileDialog_getOpenFileName);
    api->register_function("qt", "fileDialogGetSaveFileName", qt_fileDialog_getSaveFileName);
    api->register_function("qt", "fileDialogGetExistingDirectory", qt_fileDialog_getExistingDirectory);

    /* QColorDialog */
    api->register_function("qt", "colorDialogGetColor", qt_colorDialog_getColor);

    /* QFontDialog */
    api->register_function("qt", "fontDialogGetFont", qt_fontDialog_getFont);

    /* QInputDialog */
    api->register_function("qt", "inputDialogGetText", qt_inputDialog_getText);
    api->register_function("qt", "inputDialogGetInteger", qt_inputDialog_getInteger);

    /* Events */
    api->register_function("qt", "onClicked", qt_onClicked);
    api->register_function("qt", "onTextChanged", qt_onTextChanged);
    api->register_function("qt", "onValueChanged", qt_onValueChanged);
    api->register_function("qt", "onStateChanged", qt_onStateChanged);
    api->register_function("qt", "onTimerTimeout", qt_onTimerTimeout);
    api->register_function("qt", "onReturnPressed", qt_onReturnPressed);
    api->register_function("qt", "onItemActivated", qt_onItemActivated);
    api->register_function("qt", "onCurrentIndexChanged", qt_onCurrentIndexChanged);
    api->register_function("qt", "processCallbacks", qt_processCallbacks);

    /* Canvas API - QuickDraw Style */
    api->register_function("qt", "canvasNew", qt_canvasNew);
    api->register_function("qt", "canvasClear", qt_canvasClear);
    api->register_function("qt", "canvasSetPen", qt_canvasSetPen);
    api->register_function("qt", "canvasDrawLine", qt_canvasDrawLine);
    api->register_function("qt", "canvasDrawRect", qt_canvasDrawRect);
    api->register_function("qt", "canvasDrawCircle", qt_canvasDrawCircle);
    api->register_function("qt", "canvasFill", qt_canvasFill);
    api->register_function("qt", "canvasBeginStroke", qt_canvasBeginStroke);
    api->register_function("qt", "canvasEndStroke", qt_canvasEndStroke);
    api->register_function("qt", "canvasUndo", qt_canvasUndo);
    api->register_function("qt", "canvasGetImage", qt_canvasGetImage);
    api->register_function("qt", "canvasLassoSelect", qt_canvasLassoSelect);
}
