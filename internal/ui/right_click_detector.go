package ui

import (
	"image/color"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/driver/desktop"
	"fyne.io/fyne/v2/widget"
)

type rightClickDetector struct {
	widget.BaseWidget
	onRightTapped func(pos fyne.Position)
	tooltipFn     func() string
	canvas        fyne.Canvas
	popup         *widget.PopUp
}

func newRightClickDetector(
	onRightTapped func(pos fyne.Position),
	tooltipFn func() string,
	c fyne.Canvas,
) *rightClickDetector {
	d := &rightClickDetector{
		onRightTapped: onRightTapped,
		tooltipFn:     tooltipFn,
		canvas:        c,
	}
	d.ExtendBaseWidget(d)
	return d
}

func (d *rightClickDetector) TappedSecondary(p *fyne.PointEvent) {
	if d.onRightTapped != nil {
		d.onRightTapped(p.AbsolutePosition)
	}
}

func (d *rightClickDetector) MouseIn(ev *desktop.MouseEvent) {
	if d.tooltipFn == nil || d.canvas == nil {
		return
	}
	lbl := widget.NewLabel(d.tooltipFn())
	lbl.Wrapping = fyne.TextWrapOff
	d.popup = widget.NewPopUp(lbl, d.canvas)
	d.popup.ShowAtPosition(ev.AbsolutePosition)
}

func (d *rightClickDetector) MouseMoved(*desktop.MouseEvent) {}

func (d *rightClickDetector) MouseOut() {
	if d.popup != nil {
		d.popup.Hide()
		d.popup = nil
	}
}

func (d *rightClickDetector) CreateRenderer() fyne.WidgetRenderer {
	r := &canvas.Rectangle{FillColor: color.Transparent}
	return widget.NewSimpleRenderer(r)
}
