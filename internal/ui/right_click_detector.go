package ui

import (
	"image/color"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/driver/desktop"
	"fyne.io/fyne/v2/widget"
)

// tabHoverOverlay is a transparent full-size overlay placed over each tab.
// It shows a tooltip popup on hover; right-click is intentionally not handled.
type tabHoverOverlay struct {
	widget.BaseWidget
	tooltipFn func() string
	canvas    fyne.Canvas
	popup     *widget.PopUp
}

func newTabHoverOverlay(tooltipFn func() string, c fyne.Canvas) *tabHoverOverlay {
	d := &tabHoverOverlay{
		tooltipFn: tooltipFn,
		canvas:    c,
	}
	d.ExtendBaseWidget(d)
	return d
}

func (d *tabHoverOverlay) MouseIn(ev *desktop.MouseEvent) {
	if d.tooltipFn == nil || d.canvas == nil {
		return
	}
	lbl := widget.NewLabel(d.tooltipFn())
	lbl.Wrapping = fyne.TextWrapOff
	d.popup = widget.NewPopUp(lbl, d.canvas)
	d.popup.ShowAtPosition(ev.AbsolutePosition)
}

func (d *tabHoverOverlay) MouseMoved(*desktop.MouseEvent) {}

func (d *tabHoverOverlay) MouseOut() {
	if d.popup != nil {
		d.popup.Hide()
		d.popup = nil
	}
}

func (d *tabHoverOverlay) CreateRenderer() fyne.WidgetRenderer {
	r := &canvas.Rectangle{FillColor: color.Transparent}
	return widget.NewSimpleRenderer(r)
}
