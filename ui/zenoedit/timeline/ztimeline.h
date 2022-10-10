#ifndef __ZTIMELINE_H__
#define __ZTIMELINE_H__

#include <QtWidgets>

class ZSlider;
class ZIconLabel;

namespace Ui
{
    class Timeline;
}

class ZTimeline : public QWidget
{
    Q_OBJECT
public:
    ZTimeline(QWidget* parent = nullptr);
    QPair<int, int> fromTo() const;
    void initFromTo(int from, int to);
    bool isAlways() const;
    void setAlways(bool bOn);
    void resetSlider();
    int value() const;

signals:
    void playForward(bool bPlaying);
    void playForwardOneFrame();
    void playForwardLastFrame();
    int sliderValueChanged(int);
    void alwaysChecked();
    void run();
    void kill();

public slots:
    void onTimelineUpdate(int frameid);
    void onFrameEditted();
    void setSliderValue(int frameid);
    void setPlayButtonToggle(bool bToggle);

private:
    void initStyleSheet();
    void initSignals();
    void initBtnIcons();

    int m_frames;

    Ui::Timeline *m_ui;
};

#endif