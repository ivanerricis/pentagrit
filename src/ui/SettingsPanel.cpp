// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/SettingsPanel.h"

#include "PluginInfo.h"
#include "ui/Fonts.h"

namespace pentagrit::ui
{
namespace
{
constexpr int kCardMaxW = 780;
constexpr int kHeaderH = 56;
constexpr int kMessageH = 30;
constexpr int kRowH = 56;
constexpr int kRowGap = 6;
constexpr const char* kRepoUrl = "https://github.com/ivanerricis/pentagrit";

float textWidth(const juce::Font& f, const juce::String& text)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, text, 0.0f, 0.0f);
    return ga.getBoundingBox(0, -1, true).getWidth();
}

// A juce component base that restyles itself when attached or when the look and feel changes.
template <class Base> class Styled : public Base
{
public:
    using Base::Base;
    std::function<void(Base&, const Palette&)> style;

    void restyle()
    {
        if (style)
            style(*this, pal(*this));
    }
    void parentHierarchyChanged() override
    {
        Base::parentHierarchyChanged();
        restyle();
    }
    void lookAndFeelChanged() override
    {
        Base::lookAndFeelChanged();
        restyle();
    }
};

void styleEditor(juce::TextEditor& e, const Palette& p)
{
    e.setColour(juce::TextEditor::backgroundColourId, p.bg0);
    e.setColour(juce::TextEditor::textColourId, p.ink);
    e.setColour(juce::TextEditor::outlineColourId, p.bg3);
    e.setColour(juce::TextEditor::focusedOutlineColourId, p.ink3);
    e.setColour(juce::TextEditor::highlightColourId, p.bg4);
    e.setColour(juce::TextEditor::highlightedTextColourId, p.ink);
    e.setColour(juce::CaretComponent::caretColourId, p.ink);
}

// Section heading or note paragraph.
class TextBlock final : public juce::Component
{
public:
    enum class Kind
    {
        heading,
        note
    };
    TextBlock(Kind k, juce::String text) : kind_(k), text_(std::move(text))
    {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& p = pal(*this);
        if (kind_ == Kind::heading)
        {
            g.setColour(p.ink3);
            g.setFont(fonts::sans(10.0f, fonts::Weight::semiBold, 0.12f));
            g.drawText(text_.toUpperCase(), getLocalBounds().withTrimmedLeft(4).withTrimmedTop(8),
                       juce::Justification::centredLeft, false);
        }
        else
        {
            juce::AttributedString as;
            as.append(text_, fonts::sans(11.5f), p.ink2);
            as.setWordWrap(juce::AttributedString::byWord);
            as.draw(g, getLocalBounds().toFloat().reduced(4.0f, 2.0f));
        }
    }

private:
    Kind kind_;
    juce::String text_;
};

// Horizontal slider with optional left / right captions or a value read-out.
class SliderControl final : public juce::Component
{
public:
    SliderControl(double lo, double hi, double step, double value, std::function<juce::String(double)> format,
                  juce::String left, juce::String right, std::function<void(double)> onChange)
        : format_(std::move(format)), left_(std::move(left)), right_(std::move(right))
    {
        slider_.style = [](juce::Slider& s, const Palette& p)
        {
            s.setColour(juce::Slider::backgroundColourId, p.bg3);
            s.setColour(juce::Slider::trackColourId, p.ink2);
            s.setColour(juce::Slider::thumbColourId, p.ink);
        };
        slider_.setSliderStyle(juce::Slider::LinearHorizontal);
        slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider_.setRange(lo, hi, step);
        slider_.setValue(value, juce::dontSendNotification);
        slider_.setScrollWheelEnabled(false);
        slider_.onValueChange = [this, onChange]
        {
            repaint();
            if (onChange)
                onChange(slider_.getValue());
        };
        addAndMakeVisible(slider_);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        if (left_.isNotEmpty())
            b.removeFromLeft(34);
        b.removeFromRight(right_.isNotEmpty() || format_ ? 58 : 0);
        slider_.setBounds(b);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& p = pal(*this);
        g.setColour(p.ink2);
        g.setFont(fonts::mono(10.5f));
        if (left_.isNotEmpty())
            g.drawText(left_, getLocalBounds().removeFromLeft(34), juce::Justification::centredLeft, false);
        const auto text = format_ ? format_(slider_.getValue()) : right_;
        if (text.isNotEmpty())
            g.drawText(text, getLocalBounds().removeFromRight(58), juce::Justification::centredRight, false);
    }

private:
    Styled<juce::Slider> slider_;
    std::function<juce::String(double)> format_;
    juce::String left_, right_;
};

// User preset folder: text field, Choose..., Open folder.
class FolderControl final : public juce::Component
{
public:
    FolderControl()
    {
        edit.style = styleEditor;
        edit.setFont(fonts::mono(10.5f));
        edit.setTextToShowWhenEmpty("(default)", juce::Colours::grey);
        edit.setTitle("User preset folder");
        addAndMakeVisible(edit);
        addAndMakeVisible(choose);
        addAndMakeVisible(open);
    }
    void resized() override
    {
        auto b = getLocalBounds();
        open.setBounds(b.removeFromRight(92));
        b.removeFromRight(6);
        choose.setBounds(b.removeFromRight(80));
        b.removeFromRight(6);
        edit.setBounds(b);
    }
    Styled<juce::TextEditor> edit;
    juce::TextButton choose{"Choose..."}, open{"Open folder"};
};

// Drop-down of the visual styles (six options are too many for a segmented control).
class StyleSelector final : public juce::ComboBox
{
public:
    StyleSelector(const juce::StringArray& names, int selected, std::function<void(int)> onChange)
    {
        for (int i = 0; i < names.size(); ++i)
            addItem(names[i], i + 1);
        setSelectedItemIndex(selected, juce::dontSendNotification);
        setTitle("Style");
        setTooltip("Visual style of the plug-in window");
        ComboBox::onChange = [this, cb = std::move(onChange)] { cb(getSelectedItemIndex()); };
    }
};
} // namespace

// ================================================================================================ widgets

class SettingsPanel::Segmented final : public juce::Component
{
public:
    Segmented(juce::StringArray labels, int selected, std::function<void(int)> onChange)
        : labels_(std::move(labels)), selected_(selected), onChange_(std::move(onChange))
    {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    int selected() const noexcept { return selected_; }
    void setSelected(int i)
    {
        selected_ = juce::jlimit(0, labels_.size() - 1, i);
        repaint();
    }
    void choose(int i)
    {
        i = juce::jlimit(0, labels_.size() - 1, i);
        if (i == selected_)
            return;
        selected_ = i;
        repaint();
        if (onChange_)
            onChange_(i);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto inner = getLocalBounds().reduced(2);
        choose((e.x - inner.getX()) * labels_.size() / juce::jmax(1, inner.getWidth()));
    }
    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::leftKey)
            choose(selected_ - 1);
        else if (k == juce::KeyPress::rightKey)
            choose(selected_ + 1);
        else
            return false;
        return true;
    }
    void paint(juce::Graphics& g) override
    {
        const auto& p = pal(*this);
        g.setColour(p.bg0);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), p.rButton);
        const auto inner = getLocalBounds().toFloat().reduced(2.0f);
        const float w = inner.getWidth() / static_cast<float>(labels_.size());
        for (int i = 0; i < labels_.size(); ++i)
        {
            const auto r = juce::Rectangle<float>(inner.getX() + w * static_cast<float>(i), inner.getY(), w,
                                                  inner.getHeight());
            const bool on = i == selected_;
            if (on)
            {
                g.setColour(p.act);
                g.fillRoundedRectangle(r.reduced(0.5f, 0.0f), p.rSmall);
            }
            g.setColour(on ? p.onAct : p.ink2);
            g.setFont(fonts::sans(11.5f, on ? fonts::Weight::semiBold : fonts::Weight::medium));
            g.drawText(labels_[i], r.toNearestInt(), juce::Justification::centred, false);
        }
        if (hasKeyboardFocus(true))
        {
            g.setColour(p.ink2);
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
        }
    }

private:
    juce::StringArray labels_;
    int selected_;
    std::function<void(int)> onChange_;
};

class SettingsPanel::Switch final : public juce::Component
{
public:
    Switch(bool on, std::function<void(bool)> onChange) : on_(on), onChange_(std::move(onChange))
    {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    bool isOn() const noexcept { return on_; }
    void toggle()
    {
        on_ = !on_;
        repaint();
        if (onChange_)
            onChange_(on_);
    }
    void mouseDown(const juce::MouseEvent&) override { toggle(); }
    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey)
        {
            toggle();
            return true;
        }
        return false;
    }
    void paint(juce::Graphics& g) override
    {
        const auto& p = pal(*this);
        const auto r = getLocalBounds().withSizeKeepingCentre(38, 22).toFloat();
        g.setColour(on_ ? p.band[3] : p.bg3);
        g.fillRoundedRectangle(r, 11.0f);
        const float cx = on_ ? r.getRight() - 11.0f : r.getX() + 11.0f;
        g.setColour(on_ ? p.onBand : p.ink3);
        g.fillEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre({cx, r.getCentreY()}));
        if (hasKeyboardFocus(true))
        {
            g.setColour(p.ink2);
            g.drawRoundedRectangle(r.expanded(1.5f), 12.0f, 1.0f);
        }
    }

private:
    bool on_;
    std::function<void(bool)> onChange_;
};

// Row: label + description on the left, control on the right (optionally an extra component below).
class SettingsPanel::Row final : public juce::Component
{
public:
    Row(juce::String title, juce::String desc, bool badge)
        : title_(std::move(title)), desc_(std::move(desc)), badge_(badge)
    {
        setTitle(title_);
        setDescription(desc_);
    }
    void setControl(std::unique_ptr<juce::Component> c, int w, int h)
    {
        control_ = std::move(c);
        controlW_ = w;
        controlH_ = h;
        addAndMakeVisible(*control_);
    }
    void setExtra(std::unique_ptr<juce::Component> c, int h)
    {
        extra_ = std::move(c);
        extraH_ = h;
        addAndMakeVisible(*extra_);
    }
    juce::Component* control() noexcept { return control_.get(); }

    void resized() override
    {
        auto b = getLocalBounds().reduced(14, 0);
        const int top = extra_ != nullptr ? kRowH : getHeight();
        if (control_ != nullptr)
            control_->setBounds(juce::Rectangle<int>(b.getRight() - controlW_, 0, controlW_, controlH_)
                                    .withCentre({b.getRight() - controlW_ / 2, top / 2}));
        if (extra_ != nullptr)
            extra_->setBounds(b.getX(), kRowH - 4, b.getWidth(), extraH_);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& p = pal(*this);
        g.setColour(p.bg2);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), p.rPanel);
        if (p.panelBorderW > 0.0f)
        {
            g.setColour(p.bg3);
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(p.panelBorderW * 0.5f), p.rPanel,
                                   p.panelBorderW);
        }

        const int top = extra_ != nullptr ? kRowH : getHeight();
        auto left = juce::Rectangle<int>(14, 0, getWidth() - 28 - controlW_ - 16, top).reduced(0, 7);
        const auto titleFont = fonts::sans(13.0f, fonts::Weight::medium);
        g.setColour(p.ink);
        g.setFont(titleFont);
        auto titleRow = desc_.isEmpty() ? left : left.removeFromTop(18);
        g.drawText(title_, titleRow,
                   desc_.isEmpty() ? juce::Justification::centredLeft : juce::Justification::topLeft, false);
        if (badge_)
        {
            const auto badgeFont = fonts::sans(9.5f, fonts::Weight::medium, 0.04f);
            const float tw = textWidth(titleFont, title_);
            const float bw = textWidth(badgeFont, "saved with project") + 12.0f;
            const auto r = juce::Rectangle<float>(static_cast<float>(titleRow.getX()) + tw + 8.0f,
                                                  static_cast<float>(titleRow.getY()) + 1.0f, bw, 15.0f);
            g.setColour(p.bg3);
            g.fillRoundedRectangle(r, 7.5f);
            g.setColour(p.ink2);
            g.setFont(badgeFont);
            g.drawText("saved with project", r.toNearestInt(), juce::Justification::centred, false);
        }
        if (desc_.isNotEmpty())
        {
            juce::AttributedString as;
            as.append(desc_, fonts::sans(11.0f), p.ink3);
            as.setWordWrap(juce::AttributedString::byWord);
            as.draw(g, left.toFloat());
        }
    }

private:
    juce::String title_, desc_;
    bool badge_;
    std::unique_ptr<juce::Component> control_, extra_;
    int controlW_ = 0, controlH_ = 28, extraH_ = 0;
};

class SettingsPanel::Page final : public juce::Component
{
public:
    void add(std::unique_ptr<juce::Component> c, int h)
    {
        addAndMakeVisible(*c);
        items_.push_back({std::move(c), h});
    }
    int totalHeight() const
    {
        int h = 4;
        for (const auto& i : items_)
            h += i.height + kRowGap;
        return h + 8;
    }
    void resized() override
    {
        int y = 4;
        for (auto& i : items_)
        {
            i.comp->setBounds(0, y, getWidth(), i.height);
            y += i.height + kRowGap;
        }
    }

private:
    struct Item
    {
        std::unique_ptr<juce::Component> comp;
        int height;
    };
    std::vector<Item> items_;
};

// ================================================================================================ panel

SettingsPanel::SettingsPanel(PentagritAudioProcessor& processor)
    : processor_(processor), settings_(processor.getSettings())
{
    setTitle("Settings");
    setWantsKeyboardFocus(true);
    settings_.reloadIfChanged();

    tabs_ = std::make_unique<Segmented>(juce::StringArray{"Interface", "Audio", "Presets", "About & Support"},
                                        tab_, [this](int i) { showTab(i); });
    tabs_->setTitle("Settings tabs");
    addAndMakeVisible(*tabs_);
    addAndMakeVisible(done_);
    done_.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(8);
    addAndMakeVisible(viewport_);

    settings_.addChangeListener(this);
    startTimer(500);
    rebuildPage();
}

SettingsPanel::~SettingsPanel()
{
    stopTimer();
    settings_.removeChangeListener(this);
}

void SettingsPanel::showTab(int tab)
{
    tab_ = juce::jlimit(0, numTabs - 1, tab);
    tabs_->setSelected(tab_);
    message_.clear();
    rebuildPage();
    viewport_.setViewPosition(0, 0);
    repaint();
}

void SettingsPanel::say(const juce::String& text)
{
    message_ = text;
    repaint(messageArea_);
}

void SettingsPanel::apply(const std::function<void(UserSettings&)>& edit)
{
    auto s = settings_.get();
    edit(s);
    ++guard_;
    settings_.set(s);
    --guard_;
}

void SettingsPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (guard_ == 0)
        rebuildPage(); // changed from elsewhere (reset, another instance, file edit)
}

void SettingsPanel::lookAndFeelChanged()
{
    if (viewport_.getViewedComponent() != nullptr)
        rebuildPage();
}

void SettingsPanel::timerCallback()
{
    updateLatencyText();
}

void SettingsPanel::updateLatencyText()
{
    if (latencyLabel_ == nullptr)
        return;
    const int lat = processor_.getMeters().latencySamples.load(std::memory_order_relaxed);
    const double sr = processor_.getSampleRate();
    auto& apvts = processor_.getAPVTS();
    const auto os = apvts.getParameter(pid::oversampling)->getCurrentValueAsText();
    const auto phase = apvts.getParameter(pid::osPhase)->getCurrentValueAsText().toLowerCase();
    juce::String text = "Live (OS " + os + ", " + phase + "): ";
    if (sr > 0.0)
        text << juce::String(lat * 1000.0 / sr, 2) << " ms  (" << lat << " smp)";
    else
        text << lat << " smp";
    if (latencyLabel_->getText() != text)
        latencyLabel_->setText(text, juce::dontSendNotification);
}

void SettingsPanel::rebuildPage()
{
    latencyLabel_ = nullptr;
    folderEditor_ = nullptr;
    startupButton_ = nullptr;
    const auto scroll = viewport_.getViewPositionY();

    auto page = std::make_unique<Page>();
    switch (tab_)
    {
        case tabInterface:
            buildInterface(*page);
            break;
        case tabAudio:
            buildAudio(*page);
            break;
        case tabPresets:
            buildPresets(*page);
            break;
        default:
            buildAbout(*page);
            break;
    }
    viewport_.setViewedComponent(page.release(), true);
    layoutPage();
    viewport_.setViewPosition(0, scroll);
    updateLatencyText();
}

void SettingsPanel::layoutPage()
{
    if (auto* page = dynamic_cast<Page*>(viewport_.getViewedComponent()))
        page->setSize(juce::jmax(100, viewport_.getMaximumVisibleWidth()), page->totalHeight());
}

void SettingsPanel::resized()
{
    const auto b = getLocalBounds();
    const int cw = juce::jmin(kCardMaxW, b.getWidth() - 24);
    const int ch = juce::jmin(b.getHeight() - 40, 640);
    card_ = juce::Rectangle<int>(cw, ch).withCentre(b.getCentre());

    auto c = card_;
    headerArea_ = c.removeFromTop(kHeaderH);
    messageArea_ = c.removeFromBottom(kMessageH);
    auto h = headerArea_.reduced(16, 14);
    done_.setBounds(h.removeFromRight(64));
    h.removeFromRight(12);
    h.removeFromLeft(100);
    tabs_->setBounds(h.removeFromLeft(430));
    viewport_.setBounds(c.reduced(12, 0));
    layoutPage();
}

void SettingsPanel::paint(juce::Graphics& g)
{
    const auto& p = pal(*this);
    g.fillAll(juce::Colours::black.withAlpha(0.6f));
    g.setColour(p.bg1);
    g.fillRoundedRectangle(card_.toFloat(), 8.0f);
    g.setColour(p.bg3);
    g.drawRoundedRectangle(card_.toFloat().reduced(0.5f), 8.0f, 1.0f);
    g.drawHorizontalLine(headerArea_.getBottom(), static_cast<float>(card_.getX() + 1),
                         static_cast<float>(card_.getRight() - 1));
    g.setColour(p.ink);
    g.setFont(fonts::sans(15.0f, fonts::Weight::semiBold));
    g.drawText("Settings", headerArea_.withTrimmedLeft(18).withWidth(100), juce::Justification::centredLeft,
               false);
    g.setColour(p.ink2);
    g.setFont(fonts::sans(11.5f));
    g.drawText(message_, messageArea_.reduced(18, 0), juce::Justification::centredLeft, false);
}

void SettingsPanel::mouseDown(const juce::MouseEvent& e)
{
    if (!card_.contains(e.getPosition()) && onClose)
        onClose();
}

bool SettingsPanel::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }
    return false;
}

// ================================================================================================ tabs

namespace
{
template <class T> int indexOf(std::initializer_list<T> values, T v, int fallback)
{
    int i = 0;
    for (auto x : values)
    {
        if (x == v)
            return i;
        ++i;
    }
    return fallback;
}

std::unique_ptr<TextBlock> heading(const juce::String& t)
{
    return std::make_unique<TextBlock>(TextBlock::Kind::heading, t);
}
} // namespace

void SettingsPanel::buildInterface(Page& page)
{
    const auto& s = settings_.get();
    auto addRow = [&page](const juce::String& title, const juce::String& desc,
                          std::unique_ptr<juce::Component> c, int w, int h = kRowH)
    {
        auto row = std::make_unique<Row>(title, desc, false);
        row->setControl(std::move(c), w, 28);
        page.add(std::move(row), h);
    };
    auto seg = [](juce::StringArray labels, int sel, std::function<void(int)> cb)
    { return std::make_unique<Segmented>(std::move(labels), sel, std::move(cb)); };
    auto sw = [](bool on, std::function<void(bool)> cb)
    { return std::make_unique<Switch>(on, std::move(cb)); };

    static constexpr int zooms[] = {75, 100, 125, 150, 200};
    addRow("Zoom", "Scales the whole window; the aspect ratio stays fixed.",
           seg({"75%", "100%", "125%", "150%", "200%"}, indexOf({75, 100, 125, 150, 200}, s.zoomPercent, 1),
               [this](int i) { apply([i](UserSettings& u) { u.zoomPercent = zooms[i]; }); }),
           300);
    {
        juce::StringArray names;
        for (int i = 0; i < kNumStyles; ++i)
            names.add(styleName(static_cast<Style>(i)));
        addRow("Style", "Colour scheme and shapes of the whole window.",
               std::make_unique<StyleSelector>(
                   names, static_cast<int>(s.style),
                   [this](int i) { apply([i](UserSettings& u) { u.style = static_cast<Style>(i); }); }),
               190, 44);
    }
    addRow("Layout",
           "Full: crossover display plus strips (1100 x 680). Compact: no display, crossovers typed in each "
           "strip "
           "(1100 x 540).",
           seg({"Full", "Compact"}, s.layout == Layout::full ? 0 : 1, [this](int i)
               { apply([i](UserSettings& u) { u.layout = i == 0 ? Layout::full : Layout::compact; }); }),
           170, 64);
    addRow("Tooltips", "Show a description when hovering a control.",
           sw(s.tooltips, [this](bool on) { apply([on](UserSettings& u) { u.tooltips = on; }); }), 38);

    page.add(heading("Controls"), 28);
    addRow("Knob drag mode", {},
           seg({"Vertical", "Circular"}, s.knobMode == KnobMode::vertical ? 0 : 1,
               [this](int i) {
                   apply([i](UserSettings& u)
                         { u.knobMode = i == 0 ? KnobMode::vertical : KnobMode::circular; });
               }),
           200, 44);
    {
        static constexpr float steps[] = {0.0f, 0.1f, 0.5f, 1.0f};
        int cur = 0;
        for (int k = 0; k < 4; ++k)
            if (std::abs(steps[k] - s.valueStepDb) < 1.0e-4f)
                cur = k;
        addRow("Value step",
               "Drag step of the dB knobs (gain, attack, sustain, drive). Hold Shift for fine adjustment; "
               "typed values are never rounded.",
               seg({"Continuous", "0.1", "0.5", "1"}, cur,
                   [this](int i) { apply([i](UserSettings& u) { u.valueStepDb = steps[i]; }); }),
               300, 64);
    }
    addRow("Knob sensitivity", "Shift while dragging is always 5x finer.",
           std::make_unique<SliderControl>(
               0, 100, 1, static_cast<double>(s.sensitivity) * 100.0, nullptr, "Fine", "Fast",
               [this](double v)
               { apply([v](UserSettings& u) { u.sensitivity = static_cast<float>(v / 100.0); }); }),
           270);

    page.add(heading("Meters"), 28);
    static constexpr int holds[] = {0, 500, 1000, 2000, UserSettings::kInfiniteHold};
    addRow("Peak hold", {},
           seg({"Off", "0.5 s", "1 s", "2 s", "Infinite"},
               indexOf({0, 500, 1000, 2000, UserSettings::kInfiniteHold}, s.meterHoldMs, 2),
               [this](int i) { apply([i](UserSettings& u) { u.meterHoldMs = holds[i]; }); }),
           340, 44);
    addRow("Release speed", "How fast the meters fall.",
           std::make_unique<SliderControl>(
               5, 60, 1, static_cast<double>(s.meterRelease), [](double v)
               { return juce::String(juce::roundToInt(v)) + " dB/s"; }, juce::String(), juce::String(),
               [this](double v) { apply([v](UserSettings& u) { u.meterRelease = static_cast<float>(v); }); }),
           250);
    addRow("Refresh rate", {},
           seg({"30 Hz", "60 Hz"}, s.meterRefreshHz == 60 ? 1 : 0,
               [this](int i) { apply([i](UserSettings& u) { u.meterRefreshHz = i == 0 ? 30 : 60; }); }),
           170, 44);

    page.add(heading("Footer"), 28);
    addRow("Show CPU readout", {},
           sw(s.showCpu, [this](bool on) { apply([on](UserSettings& u) { u.showCpu = on; }); }), 38, 44);
}

void SettingsPanel::buildAudio(Page& page)
{
    page.add(std::make_unique<TextBlock>(TextBlock::Kind::note,
                                         "These settings are stored in the plugin state and travel with the "
                                         "project. They are not per-user."),
             24);
    auto& apvts = processor_.getAPVTS();
    {
        auto row = std::make_unique<Row>("Oversampling filter phase",
                                         "Linear phase: flat phase response, more latency. Minimum phase: "
                                         "low latency, slight phase shift near "
                                         "the filter edge.",
                                         true);
        row->setControl(
            std::make_unique<SegmentedSelector>(
                *apvts.getParameter(pid::osPhase), juce::StringArray{"Linear phase", "Minimum phase"}, 0,
                "Oversampling filter phase", "Oversampling filter phase (saved with the project)"),
            250, 28);
        page.add(std::move(row), 72);
    }
    {
        auto row =
            std::make_unique<Row>("Offline render oversampling",
                                  "Used automatically when the host renders offline or bounces.", true);
        row->setControl(std::make_unique<SegmentedSelector>(
                            *apvts.getParameter(pid::osRender),
                            juce::StringArray{"Same as live", "2x", "4x", "8x"}, 0,
                            "Offline render oversampling",
                            "Oversampling used for offline renders (saved with the project)"),
                        340, 28);
        page.add(std::move(row), 56);
    }
    {
        auto row = std::make_unique<Row>("Resulting latency", "Reported to the host and compensated.", false);
        auto label = std::make_unique<Styled<juce::Label>>();
        label->style = [](juce::Label& l, const Palette& p)
        {
            l.setColour(juce::Label::textColourId, p.ink2);
            l.setFont(fonts::mono(11.0f));
        };
        label->setJustificationType(juce::Justification::centredRight);
        latencyLabel_ = label.get();
        row->setControl(std::move(label), 330, 28);
        page.add(std::move(row), 56);
    }
}

juce::String SettingsPanel::startupLabel() const
{
    const auto& s = settings_.get();
    if (s.startupMode == StartupMode::init)
        return "Init";
    if (s.startupMode == StartupMode::specific && s.startup.isValid())
        return juce::String(s.startup.factory ? "Factory" : "User") + " / " + s.startup.category + " / " +
               s.startup.name;
    return "Last used";
}

void SettingsPanel::buildPresets(Page& page)
{
    {
        auto row = std::make_unique<Row>("User preset folder",
                                         "Sub-folders become categories in the User menu.", false);
        auto fc = std::make_unique<FolderControl>();
        fc->edit.setText(settings_.getEffectivePresetFolder().getFullPathName(), false);
        folderEditor_ = &fc->edit;
        auto commit = [this]
        {
            if (folderEditor_ == nullptr)
                return;
            const auto text = folderEditor_->getText().trim();
            const bool isDefault =
                text.isEmpty() || juce::File(text) == PresetManager::getDefaultUserFolder();
            apply([&](UserSettings& u) { u.presetFolder = isDefault ? juce::String() : text; });
            folderEditor_->setText(settings_.getEffectivePresetFolder().getFullPathName(), false);
        };
        fc->edit.onReturnKey = commit;
        fc->edit.onFocusLost = commit;
        fc->choose.onClick = [this] { chooseFolder(); };
        fc->open.onClick = [this] { openFolder(); };
        row->setControl(std::move(fc), 440, 28);
        page.add(std::move(row), 64);
    }
    {
        auto row = std::make_unique<Row>(
            "Startup preset", "Which preset is loaded when a new plugin instance is created.", false);
        auto button = std::make_unique<juce::TextButton>(startupLabel() + "  v");
        startupButton_ = button.get();
        button->onClick = [this] { showStartupMenu(*startupButton_); };
        row->setControl(std::move(button), 300, 28);
        page.add(std::move(row), 64);
    }
}

void SettingsPanel::chooseFolder()
{
    chooser_ = std::make_unique<juce::FileChooser>("Choose the user preset folder",
                                                   settings_.getEffectivePresetFolder());
    juce::Component::SafePointer<SettingsPanel> safe(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectDirectories,
                          [safe](const juce::FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (safe == nullptr || dir == juce::File())
                                  return;
                              safe->apply([&](UserSettings& u) { u.presetFolder = dir.getFullPathName(); });
                              if (safe->folderEditor_ != nullptr)
                                  safe->folderEditor_->setText(dir.getFullPathName(), false);
                              safe->say("User preset folder: " + dir.getFullPathName());
                          });
}

void SettingsPanel::openFolder()
{
    const auto dir = settings_.getEffectivePresetFolder();
    dir.createDirectory();
    dir.revealToUser();
    say("Opened " + dir.getFullPathName());
}

void SettingsPanel::showStartupMenu(juce::Component& target)
{
    juce::Component::SafePointer<SettingsPanel> safe(this);
    auto setMode = [safe](StartupMode mode, PresetRef ref)
    {
        if (safe == nullptr)
            return;
        safe->apply(
            [&](UserSettings& u)
            {
                u.startupMode = mode;
                if (mode == StartupMode::specific)
                    u.startup = ref;
            });
        if (safe->startupButton_ != nullptr)
            safe->startupButton_->setButtonText(safe->startupLabel() + "  v");
    };
    const auto& s = settings_.get();
    auto& presets = processor_.getPresetManager();
    presets.rescan();

    juce::PopupMenu menu;
    menu.addItem(juce::PopupMenu::Item("Last used")
                     .setTicked(s.startupMode == StartupMode::last)
                     .setAction([setMode] { setMode(StartupMode::last, {}); }));
    menu.addItem(juce::PopupMenu::Item("Init")
                     .setTicked(s.startupMode == StartupMode::init)
                     .setAction([setMode] { setMode(StartupMode::init, {}); }));

    auto branch = [&](bool factory)
    {
        juce::PopupMenu root;
        const auto cats = factory ? presets.getFactoryCategories() : presets.getUserCategories();
        for (const auto& cat : cats)
        {
            juce::PopupMenu sub;
            for (const auto& e : presets.getPresetsInCategory(factory, cat))
            {
                const PresetRef ref{e.name, e.category, e.factory};
                sub.addItem(juce::PopupMenu::Item(e.name)
                                .setTicked(s.startupMode == StartupMode::specific && s.startup == ref)
                                .setAction([setMode, ref] { setMode(StartupMode::specific, ref); }));
            }
            root.addSubMenu(cat, sub, sub.getNumItems() > 0);
        }
        return root;
    };
    juce::PopupMenu specific;
    specific.addSubMenu("Factory", branch(true));
    juce::PopupMenu user = branch(false);
    specific.addSubMenu("User", user, user.getNumItems() > 0);
    menu.addSubMenu("Specific preset", specific, true, nullptr, s.startupMode == StartupMode::specific);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target));
}

juce::String SettingsPanel::buildDebugInfo(PentagritAudioProcessor& p)
{
    auto& apvts = p.getAPVTS();
    auto text = [&apvts](const char* id) { return apvts.getParameter(id)->getCurrentValueAsText(); };
    const double sr = p.getSampleRate();
    const int lat = p.getMeters().latencySamples.load(std::memory_order_relaxed);
    const auto wrapper = juce::AudioProcessor::getWrapperTypeDescription(p.wrapperType);
    const juce::PluginHostType host;

    juce::StringArray lines;
    lines.add(juce::String("Pentagrit v") + info::kVersion + " (" + wrapper + ")");
    lines.add("OS: " + juce::SystemStats::getOperatingSystemName());
    lines.add("Host: " + juce::String(host.getHostDescription()));
    lines.add("Sample rate: " + (sr > 0.0 ? juce::String(sr, 0) + " Hz" : juce::String("not prepared")));
    lines.add("Block size: " +
              (p.getBlockSize() > 0 ? juce::String(p.getBlockSize()) : juce::String("not prepared")));
    lines.add("Oversampling: " + text(pid::oversampling) + ", " + text(pid::osPhase).toLowerCase() +
              " phase");
    lines.add("Latency: " + juce::String(lat) + " samples" +
              (sr > 0.0 ? " (" + juce::String(lat * 1000.0 / sr, 2) + " ms)" : juce::String()));
    lines.add("Offline oversampling: " + text(pid::osRender).toLowerCase());
    return lines.joinIntoString("\n");
}

void SettingsPanel::buildAbout(Page& page)
{
    const auto& s = settings_.get();
    auto textRow = [&page](const juce::String& title, const juce::String& desc,
                           std::unique_ptr<juce::Component> c, int w, int h = 44)
    {
        auto row = std::make_unique<Row>(title, desc, false);
        if (c != nullptr)
            row->setControl(std::move(c), w, 28);
        page.add(std::move(row), h);
    };

    textRow(juce::String("Pentagrit v") + info::kVersion, "Multiband transient shaper and saturator.",
            nullptr, 0, 56);
    {
        auto label = std::make_unique<Styled<juce::Label>>();
        label->style = [](juce::Label& l, const Palette& p)
        {
            l.setColour(juce::Label::textColourId, p.ink2);
            l.setFont(fonts::mono(11.0f));
        };
        label->setText("AGPL-3.0-or-later", juce::dontSendNotification);
        label->setJustificationType(juce::Justification::centredRight);
        textRow("Licence", {}, std::move(label), 200);
    }
    {
        auto link = std::make_unique<Styled<juce::HyperlinkButton>>("github.com/ivanerricis/pentagrit",
                                                                    juce::URL(kRepoUrl));
        link->style = [](juce::HyperlinkButton& l, const Palette& p)
        {
            l.setColour(juce::HyperlinkButton::textColourId, p.band[4]);
            l.setFont(fonts::sans(12.0f, fonts::Weight::medium), false, juce::Justification::centredRight);
        };
        textRow("Source code", {}, std::move(link), 260);
    }

    page.add(heading("Support"), 28);
    {
        auto row =
            std::make_unique<Row>("Debug information", "Include this when reporting a problem.", false);
        auto button = std::make_unique<juce::TextButton>("Copy debug info");
        juce::Component::SafePointer<juce::TextButton> safeButton(button.get());
        button->onClick = [this, safeButton]
        {
            juce::SystemClipboard::copyTextToClipboard(buildDebugInfo(processor_));
            if (safeButton != nullptr)
            {
                safeButton->setButtonText("Copied");
                juce::Timer::callAfterDelay(2000,
                                            [safeButton]
                                            {
                                                if (safeButton != nullptr)
                                                    safeButton->setButtonText("Copy debug info");
                                            });
            }
        };
        row->setControl(std::move(button), 130, 28);
        auto box = std::make_unique<Styled<juce::TextEditor>>();
        box->style = styleEditor;
        box->setMultiLine(true, false);
        box->setReadOnly(true);
        box->setFont(fonts::mono(10.5f));
        box->setTitle("Debug information");
        box->setText(buildDebugInfo(processor_), false);
        row->setExtra(std::move(box), 124);
        page.add(std::move(row), 186);
    }

    page.add(heading("Updates"), 28);
    {
        auto desc = juce::String("Off by default.");
        if (!SettingsManager::kUpdateCheckAvailable)
            desc << " Not available yet: the repository is private.";
        textRow("Check for updates on startup", desc,
                std::make_unique<Switch>(s.checkUpdates, [this](bool on)
                                         { apply([on](UserSettings& u) { u.checkUpdates = on; }); }),
                38);
        auto button = std::make_unique<juce::TextButton>("Check now");
        button->onClick = [this]
        {
            say(SettingsManager::kUpdateCheckAvailable
                    ? "Checking..."
                    : "Update check is unavailable while the repository is private. No request was made.");
        };
        textRow("Check now", "Contacts GitHub; no personal data sent.", std::move(button), 110);
    }

    page.add(heading("Reset"), 28);
    {
        auto button = std::make_unique<juce::TextButton>("Reset all settings...");
        button->onClick = [this] { confirmReset(); };
        textRow(
            "Reset all settings",
            "Resets the per-user settings (interface, controls, meters, preset folder, startup, updates). "
            "Presets and project audio settings are not changed.",
            std::move(button), 160, 64);
    }
}

void SettingsPanel::confirmReset()
{
    juce::Component::SafePointer<SettingsPanel> safe(this);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions::makeOptionsOkCancel(
                                     juce::MessageBoxIconType::QuestionIcon, "Reset all settings?",
                                     "Resets the per-user settings to their defaults. Presets and the "
                                     "project's audio settings are not "
                                     "changed.",
                                     "Reset", "Cancel", this),
                                 [safe](int r)
                                 {
                                     if (r == 1 && safe != nullptr)
                                     {
                                         safe->settings_.resetToDefaults();
                                         safe->say("Settings reset to defaults.");
                                     }
                                 });
}
} // namespace pentagrit::ui
