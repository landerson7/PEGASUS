#pragma once
#include <QWidget>

class GridWidget : public QWidget {
    Q_OBJECT
public:
    explicit GridWidget(QWidget* parent = nullptr);

    // Tweakables
    void setGridDivisions(int n) { m_divisions = (n < 2 ? 2 : n); update(); }
    void setLineWidthPx(double w) { m_lineW = w; update(); }
    void setGlow(bool on) { m_glow = on; update(); }
    void setShowDiagonal(bool on) { m_diagonal = on; update(); }
    void setShowReticle(bool on) { m_reticle = on; update(); }

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    int m_divisions = 12;       // number of squares across
    double m_lineW = 2.0;       // base line width
    bool m_glow = true;         // faux glow (draw thicker faint pass behind)
    bool m_diagonal = true;     // diagonal from center to top-right
    bool m_reticle = true;      // center target marker
};