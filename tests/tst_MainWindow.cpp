#include <QTest>
#include <QAbstractItemView>
#include <QAction>
#include <QClipboard>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSplitter>
#include <QTemporaryDir>
#include <QToolButton>
#include <algorithm>

#include "app/MainWindow.h"
#include "models/CompareSession.h"
#include "models/ImageListModel.h"
#include "widgets/BrowsePanel.h"
#include "widgets/ComparePanel.h"
#include "widgets/VlmAnnotationDialog.h"

namespace
{

bool ensureCommandBarFitsAll(MainWindow &window, int headroom = 0)
{
    auto *bar = window.findChild<QWidget *>(QStringLiteral("commandBar"));
    if (!bar) {
        return false;
    }
    auto *overflowButton = bar->findChild<QToolButton *>(QStringLiteral("commandOverflowButton"));
    auto *slider = bar->findChild<QSlider *>();
    if (!overflowButton || !slider) {
        return false;
    }

    for (int width = qMax(2400, window.width()); width <= 19200; width *= 2) {
        window.resize(width, qMax(940, window.height()));
        QCoreApplication::processEvents();
        if (!overflowButton->isVisible() && slider->width() == 150
            && bar->contentsRect().width() - bar->layout()->sizeHint().width() >= headroom) {
            return true;
        }
    }
    return false;
}

} // namespace

class tst_MainWindow : public QObject
{
    Q_OBJECT

private slots:
    void commandBar_removesDeadMenuAndBrowseButtons();
    void commandBar_usesVectorIconsAndCleanThresholdReadout();
    void commandBar_overflowsCleanlyAtMinimumWindowWidth_data();
    void commandBar_overflowsCleanlyAtMinimumWindowWidth();
    void commandBar_includesAiAnnotationButton();
    void vlmAnnotationDialog_opensModelessAndReusesWindow();
    void commandBar_compareModeButtonsControlComparePanel();
    void commandBar_reverseSwapDirectionControlsComparePanel();
    void commandBar_imageNameOverlayControlsComparePanel();
    void commandBar_copyComparisonImageButtonCopiesToClipboard();
    void directionKeys_navigateBrowseSelection();
    void browsePanel_notCollapsibleButHidableByToggle();

private:
    static QList<QListView *> sortedViews(BrowsePanel &panel);
    static void waitForRows(QListView *view, int rows);
    static void clickRow(QListView *view, int row);
    static bool isRowSelected(QListView *view, int row);
};

QList<QListView *> tst_MainWindow::sortedViews(BrowsePanel &panel)
{
    auto views = panel.findChildren<QListView *>(QStringLiteral("compareColumnListView"));
    std::sort(views.begin(), views.end(), [&panel](QListView *lhs, QListView *rhs) {
        return lhs->mapTo(&panel, QPoint(0, 0)).x() < rhs->mapTo(&panel, QPoint(0, 0)).x();
    });
    return views;
}

void tst_MainWindow::waitForRows(QListView *view, int rows)
{
    QVERIFY(view != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), rows, 8000);
}

void tst_MainWindow::clickRow(QListView *view, int row)
{
    QVERIFY(view != nullptr);
    auto *model = view->model();
    QVERIFY(model != nullptr);
    QVERIFY(row >= 0 && row < model->rowCount());

    const QModelIndex index = model->index(row, 0);
    view->scrollTo(index, QAbstractItemView::PositionAtCenter);
    QTest::qWait(30);

    const QRect rect = view->visualRect(index);
    QVERIFY2(rect.isValid(), qPrintable(QString("invalid visual rect for row %1").arg(row)));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
}

bool tst_MainWindow::isRowSelected(QListView *view, int row)
{
    auto *model = qobject_cast<ImageListModel *>(view ? view->model() : nullptr);
    if (!model || row < 0 || row >= model->imageCount()) {
        return false;
    }
    return model->isSelected(row);
}

void tst_MainWindow::commandBar_removesDeadMenuAndBrowseButtons()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QStringList commandTexts;
    const auto buttons = window.findChildren<QToolButton *>(
        QStringLiteral("commandButton"));
    for (QToolButton *button : buttons) {
        commandTexts.append(button->text());
    }

    QVERIFY(!commandTexts.contains(QStringLiteral("☰")));
    QVERIFY(!commandTexts.contains(QStringLiteral("▦  浏览")));
    QVERIFY(!commandTexts.contains(QStringLiteral("⚙  设置")));
    QVERIFY(!commandTexts.contains(QStringLiteral("⋮")));
    QVERIFY(!commandTexts.contains(QStringLiteral("⊞  添加")));
    QVERIFY(!commandTexts.contains(QStringLiteral("⟳  刷新")));
    QVERIFY(!commandTexts.contains(QStringLiteral("⌫  清空")));
    QVERIFY(commandTexts.contains(QStringLiteral("添加")));
    QVERIFY(commandTexts.contains(QStringLiteral("刷新")));
    QVERIFY(commandTexts.contains(QStringLiteral("清空")));
    QVERIFY(commandTexts.contains(QStringLiteral("上一张")));
    QVERIFY(commandTexts.contains(QStringLiteral("下一张")));
}

void tst_MainWindow::commandBar_usesVectorIconsAndCleanThresholdReadout()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QStringList expectedIconButtons = {
        QStringLiteral("添加"),
        QStringLiteral("刷新"),
        QStringLiteral("清空"),
        QStringLiteral("交换"),
        QStringLiteral("容差图"),
        QStringLiteral("上一张"),
        QStringLiteral("下一张"),
        QStringLiteral("设置"),
        QStringLiteral("AI 标注")
    };

    const auto buttons = window.findChildren<QToolButton *>(QStringLiteral("commandButton"));
    for (const QString &label : expectedIconButtons) {
        QToolButton *found = nullptr;
        for (QToolButton *button : buttons) {
            if (button->text() == label) {
                found = button;
                break;
            }
        }
        QVERIFY2(found != nullptr, qPrintable(QStringLiteral("missing command button: %1").arg(label)));
        QVERIFY2(!found->icon().isNull(), qPrintable(QStringLiteral("button missing icon: %1").arg(label)));
        QCOMPARE(found->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    }

    auto *commandBar = window.findChild<QWidget *>(QStringLiteral("commandBar"));
    QVERIFY(commandBar != nullptr);
    const auto labels = commandBar->findChildren<QLabel *>();
    for (const QLabel *label : labels) {
        QVERIFY2(label->text() != QStringLiteral("%"),
                 "0-255 pixel threshold readout should not display a misleading '%' unit");
    }

    auto *valuePill = commandBar->findChild<QLabel *>(QStringLiteral("valuePill"));
    QVERIFY(valuePill != nullptr);
    auto *slider = commandBar->findChild<QSlider *>();
    QVERIFY(slider != nullptr);
    slider->setValue(128);
    QCOMPARE(valuePill->text(), QStringLiteral("128"));
}

void tst_MainWindow::commandBar_overflowsCleanlyAtMinimumWindowWidth_data()
{
    QTest::addColumn<bool>("wideFont");
    QTest::newRow("default-font") << false;
    QTest::newRow("wide-font") << true;
}

void tst_MainWindow::commandBar_overflowsCleanlyAtMinimumWindowWidth()
{
    QFETCH(bool, wideFont);
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(ensureCommandBarFitsAll(window));

    auto *commandBar = window.findChild<QWidget *>(QStringLiteral("commandBar"));
    QVERIFY(commandBar != nullptr);
    auto *overflowButton = commandBar->findChild<QToolButton *>(QStringLiteral("commandOverflowButton"));
    QVERIFY(overflowButton != nullptr);
    auto *optionsEndSeparator = commandBar->findChild<QWidget *>(QStringLiteral("optionsEndSeparator"));
    QVERIFY(optionsEndSeparator != nullptr);
    auto *slider = commandBar->findChild<QSlider *>();
    QVERIFY(slider != nullptr);
    const auto buttons = commandBar->findChildren<QToolButton *>(QStringLiteral("commandButton"));

    const int frameWidth = commandBar->width() - commandBar->contentsRect().width();
    if (wideFont) {
        const int normalFitWidth = commandBar->layout()->sizeHint().width() + frameWidth;
        commandBar->setMaximumWidth(normalFitWidth);
        QTRY_COMPARE(commandBar->width(), normalFitWidth);
        QVERIFY(!overflowButton->isVisible());
        // Widen the text-only commands with per-button font changes only (no
        // command-bar stylesheet change) to verify child LayoutRequest events
        // trigger overflow recalculation at a fixed toolbar width, and that
        // restoring a hidden overflowed button's font restores it to the bar.
        QList<QPair<QToolButton *, QFont>> originalFonts;
        for (QToolButton *button : buttons) {
            if (button->icon().isNull()) {
                QFont font = button->font();
                originalFonts.append({button, font});
                font.setLetterSpacing(QFont::AbsoluteSpacing, 36);
                button->setFont(font);
            }
        }
        QTRY_VERIFY(overflowButton->isVisible());
        QCOMPARE(commandBar->width(), normalFitWidth);
        for (const auto &entry : originalFonts) {
            entry.first->setFont(entry.second);
        }
        QTRY_VERIFY(!overflowButton->isVisible());
        for (const auto &entry : originalFonts) {
            QFont font = entry.second;
            font.setLetterSpacing(QFont::AbsoluteSpacing, 36);
            entry.first->setFont(font);
        }
        QTRY_VERIFY(overflowButton->isVisible());
        commandBar->setMaximumWidth(QWIDGETSIZE_MAX);
        QVERIFY(ensureCommandBarFitsAll(window));
        QVERIFY(commandBar->layout()->sizeHint().width() + frameWidth > 1680);
    }

    auto verifyNoSqueezedButtons = [&]() {
        const QRect barRect = commandBar->rect();
        for (QToolButton *button : buttons) {
            if (button->isVisible()) {
                QVERIFY2(button->width() >= button->sizeHint().width(),
                         qPrintable(QStringLiteral("button '%1' squeezed to %2px < sizeHint %3px (bar=%4px)")
                                        .arg(button->text())
                                        .arg(button->width())
                                        .arg(button->sizeHint().width())
                                        .arg(commandBar->width())));
                QVERIFY2(barRect.contains(button->geometry()),
                         qPrintable(QStringLiteral("button '%1' clipped outside commandBar (btn=%2..%3, bar=%4)")
                                        .arg(button->text())
                                        .arg(button->geometry().left())
                                        .arg(button->geometry().right())
                                        .arg(barRect.right())));
            }
        }
        if (overflowButton->isVisible()) {
            QVERIFY(overflowButton->width() >= overflowButton->sizeHint().width());
            QVERIFY(barRect.contains(overflowButton->geometry()));
        }
        QVERIFY(slider->width() >= 88 && slider->width() <= 150);
    };

    // Derive the independent exact width where all items fit with the slider at
    // its minimum width (88px) before narrowing, then confirm that width fits all
    // buttons with zero overflow while 1px narrower triggers overflow.
    QVERIFY(!overflowButton->isVisible());
    QCOMPARE(slider->width(), 150);
    const auto minimumWidthForVisibleItems = [&]() {
        return commandBar->layout()->sizeHint().width() + frameWidth - (slider->width() - 88);
    };
    const int expectedExactFitWidth = minimumWidthForVisibleItems();

    commandBar->setMaximumWidth(expectedExactFitWidth);
    QTRY_COMPARE(commandBar->width(), expectedExactFitWidth);
    QCOMPARE(slider->width(), 88);
    QVERIFY(!overflowButton->isVisible());
    QVERIFY(optionsEndSeparator->isVisible());
    for (QToolButton *button : buttons) {
        QVERIFY(button->isVisible());
    }
    verifyNoSqueezedButtons();

    const int firstOverflowWidth = expectedExactFitWidth - 1;
    commandBar->setMaximumWidth(firstOverflowWidth);
    QTRY_COMPARE(commandBar->width(), firstOverflowWidth);
    QVERIFY(overflowButton->isVisible());
    QVERIFY(optionsEndSeparator->isVisible());
    verifyNoSqueezedButtons();

    QMenu *overflowMenu = overflowButton->menu();
    QVERIFY(overflowMenu != nullptr);
    QVERIFY(!overflowMenu->actions().isEmpty());

    QAction *targetAction = nullptr;
    for (QAction *action : overflowMenu->actions()) {
        if (action->isCheckable()) {
            targetAction = action;
            break;
        }
    }
    QVERIFY(targetAction != nullptr);
    const bool initialChecked = targetAction->isChecked();
    QSignalSpy popupHidden(overflowMenu, &QMenu::aboutToHide);
    QSignalSpy actionTriggered(targetAction, &QAction::triggered);
    struct HideCounter final : QObject {
        int count = 0;

        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Hide) {
                ++count;
            }
            return false;
        }
    } overflowButtonHidden;
    overflowButton->installEventFilter(&overflowButtonHidden);

    overflowMenu->popup(overflowButton->mapToGlobal(QPoint(0, overflowButton->height())));
    QTRY_VERIFY(overflowMenu->isVisible());
    overflowMenu->setActiveAction(targetAction);
    QCOMPARE(overflowMenu->activeAction(), targetAction);

    // Cross each exact cutoff until the options group has fully overflowed.
    // The list grows on each resize and the selected action stays active.
    while (optionsEndSeparator->isVisible()) {
        const int nextOverflowWidth = minimumWidthForVisibleItems() - 1;
        QVERIFY(nextOverflowWidth < commandBar->width());
        const qsizetype beforeCount = overflowMenu->actions().size();
        commandBar->setMaximumWidth(nextOverflowWidth);
        QTRY_COMPARE(commandBar->width(), nextOverflowWidth);
        QVERIFY(overflowMenu->actions().size() > beforeCount);
        QVERIFY(overflowMenu->actions().contains(targetAction));
        QVERIFY(overflowMenu->isVisible());
        QCOMPARE(popupHidden.count(), 0);
        QTRY_COMPARE(overflowMenu->activeAction(), targetAction);
    }
    QVERIFY(overflowButton->isVisible());
    QVERIFY(!optionsEndSeparator->isVisible());
    verifyNoSqueezedButtons();

    // The current actions also stay selected when the resize only shrinks the slider.
    const QList<QAction *> beforeActions = overflowMenu->actions();
    const int sameActionsWidth = minimumWidthForVisibleItems();
    QVERIFY(sameActionsWidth < commandBar->width());
    commandBar->setMaximumWidth(sameActionsWidth);
    QTRY_COMPARE(commandBar->width(), sameActionsWidth);
    QCOMPARE(slider->width(), 88);
    QCOMPARE(overflowMenu->actions(), beforeActions);
    QVERIFY(overflowMenu->isVisible());
    QCOMPARE(popupHidden.count(), 0);
    QCOMPARE(overflowButtonHidden.count, 0);
    QTRY_COMPARE(overflowMenu->activeAction(), targetAction);
    verifyNoSqueezedButtons();

    QTest::keyClick(overflowMenu, Qt::Key_Return);
    QTRY_COMPARE(actionTriggered.count(), 1);
    QTRY_COMPARE(targetAction->isChecked(), !initialChecked);
    QTRY_VERIFY(!overflowMenu->isVisible());

    // Widen to the exact fit from the overflow state with the popup still open.
    overflowMenu->popup(overflowButton->mapToGlobal(QPoint(0, overflowButton->height())));
    QTRY_VERIFY(overflowMenu->isVisible());

    commandBar->setMaximumWidth(expectedExactFitWidth);
    QTRY_COMPARE(commandBar->width(), expectedExactFitWidth);
    QTRY_VERIFY(!overflowMenu->isVisible());
    for (QToolButton *button : buttons) {
        QTRY_VERIFY_WITH_TIMEOUT(button->isVisible(), 1000);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!overflowButton->isVisible(), 1000);
    QTRY_VERIFY(optionsEndSeparator->isVisible());
    QCOMPARE(slider->width(), 88);
    verifyNoSqueezedButtons();

    QToolButton *restoredButton = nullptr;
    for (QToolButton *button : buttons) {
        if (button->defaultAction() == targetAction) {
            restoredButton = button;
            break;
        }
    }
    QVERIFY(restoredButton != nullptr);
    QTRY_COMPARE(restoredButton->isChecked(), !initialChecked);

    // Also verify minimum window width (1280x800) with no artificial commandBar cap.
    commandBar->setMaximumWidth(QWIDGETSIZE_MAX);
    window.resize(1280, 800);
    QTRY_COMPARE(window.size(), QSize(1280, 800));
    QCoreApplication::processEvents();
    verifyNoSqueezedButtons();
}

void tst_MainWindow::commandBar_includesAiAnnotationButton()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QStringList commandTexts;
    const auto buttons = window.findChildren<QToolButton *>(
        QStringLiteral("commandButton"));
    for (QToolButton *button : buttons) {
        commandTexts.append(button->text());
    }

    QVERIFY(commandTexts.contains(QStringLiteral("AI 标注")));
    QVERIFY(commandTexts.contains(QStringLiteral("设置")));
    QVERIFY(!commandTexts.contains(QStringLiteral("VLM 设置")));
}

void tst_MainWindow::vlmAnnotationDialog_opensModelessAndReusesWindow()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QToolButton *annotationButton = nullptr;
    const auto buttons = window.findChildren<QToolButton *>(
        QStringLiteral("commandButton"));
    for (QToolButton *button : buttons) {
        if (button->text() == QStringLiteral("AI 标注")) {
            annotationButton = button;
            break;
        }
    }
    QVERIFY(annotationButton != nullptr);

    QTest::mouseClick(annotationButton, Qt::LeftButton);

    VlmAnnotationDialog *dialog = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((dialog = window.findChild<VlmAnnotationDialog *>()) != nullptr, 1000);
    QVERIFY(!dialog->isModal());
    QCOMPARE(dialog->windowModality(), Qt::NonModal);
    QVERIFY(dialog->windowFlags() & Qt::WindowMinimizeButtonHint);
    QVERIFY(window.isEnabled());

    auto *minimizeButton = dialog->findChild<QPushButton *>(QStringLiteral("vlmMinimizeButton"));
    QVERIFY(minimizeButton != nullptr);
    QVERIFY(minimizeButton->isEnabled());

    QTest::mouseClick(annotationButton, Qt::LeftButton);
    QCOMPARE(window.findChildren<VlmAnnotationDialog *>().size(), 1);

    dialog->close();
}

void tst_MainWindow::commandBar_compareModeButtonsControlComparePanel()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *comparePanel = window.findChild<ComparePanel *>();
    QVERIFY(comparePanel != nullptr);
    QCOMPARE(comparePanel->compareMode(), ComparePanel::SwapMode);

    QToolButton *toleranceButton = nullptr;
    QToolButton *swapButton = nullptr;
    const auto buttons = window.findChildren<QToolButton *>(
        QStringLiteral("commandButton"));
    for (QToolButton *button : buttons) {
        if (button->text() == QStringLiteral("容差图")) {
            toleranceButton = button;
        } else if (button->text() == QStringLiteral("交换")) {
            swapButton = button;
        }
    }
    QVERIFY(toleranceButton != nullptr);
    QVERIFY(swapButton != nullptr);

    QTest::mouseClick(toleranceButton, Qt::LeftButton);
    QCOMPARE(comparePanel->compareMode(), ComparePanel::ToleranceMode);

    QTest::mouseClick(swapButton, Qt::LeftButton);
    QCOMPARE(comparePanel->compareMode(), ComparePanel::SwapMode);
}

void tst_MainWindow::commandBar_reverseSwapDirectionControlsComparePanel()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *comparePanel = window.findChild<ComparePanel *>();
    QVERIFY(comparePanel != nullptr);

    QToolButton *reverseButton = nullptr;
    const auto buttons = window.findChildren<QToolButton *>(
        QStringLiteral("commandButton"));
    for (QToolButton *button : buttons) {
        if (button->text() == QStringLiteral("反向交换")) {
            reverseButton = button;
            break;
        }
    }
    QVERIFY(reverseButton != nullptr);
    QCOMPARE(reverseButton->isChecked(), comparePanel->reverseSwapDirectionEnabled());

    const bool originalDirection = comparePanel->reverseSwapDirectionEnabled();
    QVERIFY(ensureCommandBarFitsAll(window));
    QVERIFY(reverseButton->isVisible());
    QTest::mouseClick(reverseButton, Qt::LeftButton);
    QCOMPARE(comparePanel->reverseSwapDirectionEnabled(), !originalDirection);
    QCOMPARE(reverseButton->isChecked(), !originalDirection);
}

void tst_MainWindow::commandBar_imageNameOverlayControlsComparePanel()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *comparePanel = window.findChild<ComparePanel *>();
    QVERIFY(comparePanel != nullptr);

    QToolButton *overlayButton = nullptr;
    const auto buttons = window.findChildren<QToolButton *>(
        QStringLiteral("commandButton"));
    for (QToolButton *button : buttons) {
        if (button->text() == QStringLiteral("名称标注")) {
            overlayButton = button;
            break;
        }
    }
    QVERIFY(overlayButton != nullptr);
    QCOMPARE(overlayButton->isChecked(), comparePanel->imageNameOverlayEnabled());

    const bool originallyEnabled = comparePanel->imageNameOverlayEnabled();
    QVERIFY(ensureCommandBarFitsAll(window));
    QVERIFY(overlayButton->isVisible());
    QTest::mouseClick(overlayButton, Qt::LeftButton);
    QCOMPARE(comparePanel->imageNameOverlayEnabled(), !originallyEnabled);
    QCOMPARE(overlayButton->isChecked(), !originallyEnabled);
}

void tst_MainWindow::directionKeys_navigateBrowseSelection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::yellow);
    QVERIFY(image.save(dir.filePath("img_00.png")));
    QVERIFY(image.save(dir.filePath("img_01.png")));

    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *session = window.findChild<CompareSession *>();
    auto *browsePanel = window.findChild<BrowsePanel *>();
    QVERIFY(session != nullptr);
    QVERIFY(browsePanel != nullptr);

    QVERIFY(session->addFolder(dir.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(*browsePanel), views.size() == 1), 5000);
    waitForRows(views[0], 2);

    clickRow(views[0], 0);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], 0), 1000);

    views[0]->setFocus();
    QTest::keyClick(views[0], Qt::Key_Down);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], 1), 1000);

    QTest::keyClick(views[0], Qt::Key_Up);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], 0), 1000);
}

void tst_MainWindow::browsePanel_notCollapsibleButHidableByToggle()
{
    MainWindow window;
    window.resize(1680, 940);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *splitter = window.findChild<QSplitter *>(QStringLiteral("contentSplitter"));
    QVERIFY(splitter != nullptr);
    auto *browse = window.findChild<BrowsePanel *>();
    QVERIFY(browse != nullptr);

    // Dragging the handle to the edge must NOT collapse the browse panel; it stops
    // at the panel's minimum (the thumbnails just reach their minimum size).
    QVERIFY(!splitter->isCollapsible(1));
    QVERIFY(!browse->isHidden());
    QVERIFY(splitter->sizes().at(1) > 0);

    QAction *toggle = nullptr;
    const QList<QAction *> actions = window.findChildren<QAction *>();
    for (QAction *action : actions) {
        if (action->text() == QStringLiteral("Browse Panel")) {
            toggle = action;
            break;
        }
    }
    QVERIFY(toggle != nullptr);

    // The toggle still fully hides the panel (it hides the widget rather than
    // collapsing, which a non-collapsible pane would refuse)...
    toggle->trigger();
    QTRY_VERIFY(browse->isHidden());
    QTRY_COMPARE(splitter->sizes().at(1), 0);

    // ...and restores it (visible, non-zero width).
    toggle->trigger();
    QTRY_VERIFY(!browse->isHidden());
    QTRY_VERIFY(splitter->sizes().at(1) > 0);
}

void tst_MainWindow::commandBar_copyComparisonImageButtonCopiesToClipboard()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString folderA = root.filePath(QStringLiteral("set_a"));
    const QString folderB = root.filePath(QStringLiteral("set_b"));
    QVERIFY(QDir().mkpath(folderA));
    QVERIFY(QDir().mkpath(folderB));

    QImage imgA(48, 48, QImage::Format_ARGB32);
    imgA.fill(QColor(210, 40, 40));
    const QString pathA = folderA + QStringLiteral("/sample_01.png");
    QVERIFY(imgA.save(pathA));

    QImage imgB(48, 48, QImage::Format_ARGB32);
    imgB.fill(QColor(40, 90, 210));
    const QString pathB = folderB + QStringLiteral("/sample_02.png");
    QVERIFY(imgB.save(pathB));

    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QToolButton *copyButton = nullptr;
    const auto buttons = window.findChildren<QToolButton *>(
        QStringLiteral("commandButton"));
    for (QToolButton *button : buttons) {
        if (button->text() == QStringLiteral("复制对比图")) {
            copyButton = button;
            break;
        }
    }
    QVERIFY(copyButton != nullptr);
    const int feedbackWidth = copyButton->fontMetrics().horizontalAdvance(QStringLiteral("无可复制图片"));
    QVERIFY(ensureCommandBarFitsAll(window, feedbackWidth));

    // Clicking when no images are compared should show non-blocking feedback and not copy.
    QGuiApplication::clipboard()->clear();
    QVERIFY(copyButton->isVisible());
    QTest::mouseClick(copyButton, Qt::LeftButton);
    QCOMPARE(copyButton->text(), QStringLiteral("无可复制图片"));
    QVERIFY(QGuiApplication::clipboard()->image().isNull());

    auto *session = window.findChild<CompareSession *>();
    auto *comparePanel = window.findChild<ComparePanel *>();
    QVERIFY(session != nullptr);
    QVERIFY(comparePanel != nullptr);

    QVERIFY(session->addFolder(folderA));
    QVERIFY(session->addFolder(folderB));
    comparePanel->setSelectedImages({{folderA, pathA}, {folderB, pathB}});
    QTRY_VERIFY_WITH_TIMEOUT(!comparePanel->buildComparisonImage().isNull(), 3000);

    QVERIFY(copyButton->isVisible());
    QTest::mouseClick(copyButton, Qt::LeftButton);
    QCOMPARE(copyButton->text(), QStringLiteral("✓ 已复制"));

    const QImage copied = QGuiApplication::clipboard()->image();
    QVERIFY(!copied.isNull());
    QVERIFY(copied.width() > imgA.width());
    QVERIFY(copied.height() > imgA.height());
}

QTEST_MAIN(tst_MainWindow)
#include "tst_MainWindow.moc"
