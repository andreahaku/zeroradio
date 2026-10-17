/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "sdr_viewmodel.h"
#include "nav_hints.h"

#include "ui_const.h"

#include <cstdio>

namespace sdr {
namespace {

// Format a frequency (Hz) as "145.5000 MHz" (4 decimals = 100 Hz resolution).
void format_vfo(char* out, size_t out_size, int32_t hz) {
    const int32_t mhz = hz / 1000000;
    const int32_t frac = (hz % 1000000) / 100; // 0..9999 -> tenths of kHz
    std::snprintf(out, out_size, "%d.%04d MHz", static_cast<int>(mhz), static_cast<int>(frac));
}

// 3 decimals, no "MHz" suffix (band-edge and side grid-line labels).
void format_freq_plain(char* out, size_t out_size, int32_t hz) {
    std::snprintf(out, out_size, "%d.%03d",
                  static_cast<int>(hz / 1000000), static_cast<int>((hz % 1000000) / 1000));
}

// Short label for a tuning step (Hz): "100k", "10k", "1k", "500", "100".
std::string format_step(int32_t hz) {
    char buf[8];
    if (hz >= 1000 && hz % 1000 == 0) {
        std::snprintf(buf, sizeof(buf), "%dk", static_cast<int>(hz / 1000));
    } else {
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(hz));
    }
    return buf;
}

} // namespace

SdrViewModel::SdrViewModel() {
    set_help_doc("docs/help/sdr.md"); // H
    set_nav_provider(this);
    set_title("SDR");

    // Sync the generic shell + SDR subjects from the persisted model.
    set_dark_mode(model_.dark_mode());
    mode_text_subject_.set(radio_mode_name(model_.mode()));
    freq_grid_subject_.set(model_.freq_grid());
    time_grid_subject_.set(model_.time_grid());
    peak_hold_subject_.set(model_.peak_hold());
    fine_tune_subject_.set(model_.fine_tune());
    muted_subject_.set(model_.muted());
    volume_subject_.set(model_.volume());
    publish_vfo();
    publish_gain();
}

// --- subject accessors ---
lv_subject_t* SdrViewModel::vfo_text_subject()       { return vfo_text_subject_.native(); }
lv_subject_t* SdrViewModel::mode_text_subject()      { return mode_text_subject_.native(); }
lv_subject_t* SdrViewModel::smeter_subject()         { return smeter_subject_.native(); }
lv_subject_t* SdrViewModel::band_low_text_subject()  { return band_low_text_subject_.native(); }
lv_subject_t* SdrViewModel::band_high_text_subject() { return band_high_text_subject_.native(); }
lv_subject_t* SdrViewModel::grid_low_text_subject()  { return grid_low_text_subject_.native(); }
lv_subject_t* SdrViewModel::grid_high_text_subject() { return grid_high_text_subject_.native(); }
lv_subject_t* SdrViewModel::freq_grid_subject()      { return freq_grid_subject_.native(); }
lv_subject_t* SdrViewModel::time_grid_subject()      { return time_grid_subject_.native(); }
lv_subject_t* SdrViewModel::peak_hold_subject()      { return peak_hold_subject_.native(); }
lv_subject_t* SdrViewModel::wf_split_subject()       { return wf_split_subject_.native(); }
lv_subject_t* SdrViewModel::freq_input_req_subject() { return freq_input_req_subject_.native(); }
lv_subject_t* SdrViewModel::fine_tune_subject()      { return fine_tune_subject_.native(); }
lv_subject_t* SdrViewModel::muted_subject()          { return muted_subject_.native(); }
lv_subject_t* SdrViewModel::volume_subject()         { return volume_subject_.native(); }
lv_subject_t* SdrViewModel::gain_text_subject()      { return gain_text_subject_.native(); }

// --- actions ---
void SdrViewModel::tune_up()   { model_.tune_up();   publish_vfo(); }
void SdrViewModel::tune_down() { model_.tune_down(); publish_vfo(); }

void SdrViewModel::set_vfo_hz(int64_t hz) {
    model_.set_vfo_hz(hz);
    publish_vfo();
}

void SdrViewModel::cycle_mode() {
    model_.cycle_mode();
    publish_mode();
    bump_nav_refresh(); // the tuning-step label depends on the mode
}

void SdrViewModel::toggle_fine_tune() {
    model_.toggle_fine_tune();
    fine_tune_subject_.set(model_.fine_tune());
    bump_nav_refresh(); // the step label changes
}

void SdrViewModel::set_smeter(int level) {
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    smeter_subject_.set(level);
}

void SdrViewModel::request_freq_input() {
    freq_input_req_subject_.set(freq_input_req_subject_.value() + 1);
}

void SdrViewModel::zoom_in()   { model_.zoom_in();   publish_vfo(); }
void SdrViewModel::zoom_out()  { model_.zoom_out();  publish_vfo(); }
void SdrViewModel::cycle_band(){ model_.cycle_band(); publish_vfo(); }

void SdrViewModel::toggle_freq_grid() { model_.toggle_freq_grid(); publish_grids(); }
void SdrViewModel::toggle_time_grid() { model_.toggle_time_grid(); publish_grids(); }

void SdrViewModel::toggle_peak_hold() {
    model_.toggle_peak_hold();
    peak_hold_subject_.set(model_.peak_hold());
}

void SdrViewModel::cycle_wf_split() {
    // Waterfall share (%) of the spectrum+waterfall area. Key 7 steps the split
    // down (more spectrum line, less waterfall), wrapping back to full waterfall
    // after "none": mostly -> half -> none -> full -> ...
    static constexpr int kPresets[] = {80, 50, 0, 100};
    wf_split_idx_ = (wf_split_idx_ + 1) % 4;
    wf_split_subject_.set(kPresets[wf_split_idx_]);
}

void SdrViewModel::toggle_mute() {
    model_.toggle_mute();
    publish_audio();
    bump_nav_refresh(); // the mute icon reflects state
}

void SdrViewModel::volume_up() {
    model_.volume_up();
    publish_audio();
    bump_nav_refresh(); // the volume readout
}

void SdrViewModel::volume_down() {
    model_.volume_down();
    publish_audio();
    bump_nav_refresh();
}

std::string SdrViewModel::on_volume(int step) {
    if (step == 0) toggle_mute();
    else if (step > 0) volume_up();
    else volume_down();
    if (muted()) {
        return toolkit::i18n::pick(
            {"Muted", "Muto", "Muet", "Stumm", "Silencio", "ミュート", "静音"});
    }
    return std::string(toolkit::i18n::pick({"Volume", "Volume", "Volume", "Lautstärke", "Volumen",
                                            "音量", "音量"})) +
           " " + std::to_string(model_.volume()) + "%";
}

void SdrViewModel::toggle_gain_auto() { model_.toggle_gain_auto(); publish_gain(); bump_nav_refresh(); }
void SdrViewModel::gain_up()          { model_.gain_up();          publish_gain(); bump_nav_refresh(); }
void SdrViewModel::gain_down()        { model_.gain_down();        publish_gain(); bump_nav_refresh(); }

void SdrViewModel::toggle_dark() {
    model_.toggle_dark_mode();
    set_dark_mode(model_.dark_mode()); // updates the shell subject (NavBar/theme observe it)
}

// --- read-only state ---
bool    SdrViewModel::fine_tune() const     { return model_.fine_tune(); }
int32_t SdrViewModel::tune_step_hz() const  { return model_.tune_step_hz(); }
bool    SdrViewModel::peak_hold() const     { return model_.peak_hold(); }
bool    SdrViewModel::muted() const         { return model_.muted(); }
float   SdrViewModel::volume_gain() const   { return static_cast<float>(model_.volume()) / 100.0f; }
bool    SdrViewModel::gain_auto() const     { return model_.gain_auto(); }
int32_t SdrViewModel::gain_tenth_db() const { return model_.gain_tenth_db(); }
int32_t SdrViewModel::span_khz() const      { return model_.span_khz(); }
int32_t SdrViewModel::vfo_hz() const        { return model_.vfo_hz(); }
RadioMode SdrViewModel::mode() const        { return model_.mode(); }
Passband  SdrViewModel::passband() const    { return model_.passband(); }

// --- NavProvider ---
int SdrViewModel::nav_page_count() const {
    return 5;
}

void SdrViewModel::nav_fill(int page, NavProvider::NavSlot out[5]) const {
    out[0] = {"#", true, true}; // page number (text overridden by the NavBar)
    const bool dark = is_dark_mode();

    switch (static_cast<Page>(page)) {
        case Page::Tuning:
            step_label_ = format_step(model_.tune_step_hz());
            out[1] = {view::ICON_CARET_LEFT, false, true};   // tune down
            out[2] = {view::ICON_KEYBOARD, false, true};     // frequency entry
            out[3] = {view::ICON_CARET_RIGHT, false, true};  // tune up
            out[4] = {step_label_.c_str(), true, true};      // coarse/fine toggle + step
            break;
        case Page::Zoom:
            out[1] = {view::ICON_MINUS, false, true};        // zoom out
            out[2] = {view::ICON_BAND, false, true};         // band
            out[3] = {view::ICON_PLUS, false, true};         // zoom in
            out[4] = {view::ICON_MODE, false, true};         // demod mode
            break;
        case Page::Visual:
            out[1] = {dark ? view::ICON_SUN : view::ICON_MOON, false, true}; // theme
            out[2] = {view::ICON_GRID_FREQ, false, true};    // freq grid
            out[3] = {view::ICON_WF_SPLIT, false, true};     // waterfall/spectrum split
            out[4] = {view::ICON_PEAK, false, true};         // peak hold
            break;
        case Page::Audio:
            vol_label_ = std::to_string(model_.volume()) + "%";
            out[1] = {muted() ? view::ICON_SPEAKER_MUTE : view::ICON_SPEAKER, false, true};
            out[2] = {view::ICON_MINUS, false, true};        // volume down
            out[3] = {view::ICON_PLUS, false, true};         // volume up
            out[4] = {vol_label_.c_str(), true, true};       // volume readout (display only)
            break;
        case Page::Settings:
            gain_label_ = gain_auto() ? std::string("auto")
                                      : (std::to_string(gain_tenth_db() / 10) + "d");
            out[1] = {view::ICON_MINUS, false, true};        // gain down
            out[2] = {view::ICON_PLUS, false, true};         // gain up
            out[3] = {gain_label_.c_str(), true, true};      // gain readout + auto toggle
            out[4] = {view::ICON_SIGN_OUT, false, true};     // exit
            break;
    }
}

void SdrViewModel::nav_activate(int page, int slot) {
    switch (static_cast<Page>(page)) {
        case Page::Tuning:
            if (slot == 1) tune_down();
            else if (slot == 2) request_freq_input();
            else if (slot == 3) tune_up();
            else if (slot == 4) toggle_fine_tune();
            break;
        case Page::Zoom:
            if (slot == 1) zoom_out();
            else if (slot == 2) cycle_band();
            else if (slot == 3) zoom_in();
            else if (slot == 4) cycle_mode();
            break;
        case Page::Visual:
            if (slot == 1) toggle_dark();
            else if (slot == 2) toggle_freq_grid();
            else if (slot == 3) cycle_wf_split();
            else if (slot == 4) toggle_peak_hold();
            break;
        case Page::Audio:
            if (slot == 1) toggle_mute();
            else if (slot == 2) volume_down();
            else if (slot == 3) volume_up();
            // slot 4 is the display-only volume readout
            break;
        case Page::Settings:
            if (slot == 1) gain_down();
            else if (slot == 2) gain_up();
            else if (slot == 3) toggle_gain_auto();
            else if (slot == 4) request_quit();
            break;
    }
}

const char* SdrViewModel::nav_page_name(int page) const {
    switch (static_cast<Page>(page)) {
        case Page::Tuning: return "TUNE";
        case Page::Zoom: return "ZOOM";
        case Page::Visual: return "VIEW";
        case Page::Audio: return "AUDIO";
        case Page::Settings: return "GAIN";
    }
    return nullptr;
}

toolkit::i18n::Text SdrViewModel::nav_hint(int page, int slot) const {
    namespace h = toolkit::hints;
    switch (static_cast<Page>(page)) {
        case Page::Tuning:
            switch (slot) {
                case 1: return {"Tune down by one step", "Scendi di un passo di sintonia",
                                 "Descend d’un pas de réglage", "Einen Schritt tiefer abstimmen",
                                 "Baja la sintonía un paso",
                                 "1ステップ下へ同調", "向下调谐一步"};
                case 2: return {"Type a frequency in MHz", "Inserisci una frequenza in MHz",
                                 "Saisit une fréquence en MHz", "Frequenz in MHz eingeben",
                                 "Introduce una frecuencia en MHz",
                                 "周波数を MHz で入力", "以 MHz 输入频率"};
                case 3: return {"Tune up by one step", "Sali di un passo di sintonia",
                                 "Monte d’un pas de réglage", "Einen Schritt höher abstimmen",
                                 "Sube la sintonía un paso",
                                 "1ステップ上へ同調", "向上调谐一步"};
                case 4: return {"Switch between the coarse and the fine tuning step",
                                 "Alterna il passo di sintonia grande e quello fine",
                                 "Alterne entre le pas de réglage large et le pas fin",
                                 "Wechselt zwischen grober und feiner Schrittweite",
                                 "Alterna entre el paso de sintonía grande y el fino",
                                 "同調ステップの粗い／細かいを切り替え", "切换粗调和细调步进"};
            }
            break;
        case Page::Zoom:
            switch (slot) {
                case 1: return h::kZoomOut;
                case 2: return {"Jump to the next band: FM, Air, 2m, 70cm",
                                 "Passa alla banda successiva: FM, Air, 2m, 70cm",
                                 "Passe à la bande suivante : FM, Air, 2m, 70cm",
                                 "Springt zum nächsten Band: FM, Air, 2m, 70cm",
                                 "Pasa a la banda siguiente: FM, Air, 2m, 70cm",
                                 "次のバンドへ：FM、Air、2m、70cm", "切换到下一个波段：FM、Air、2m、70cm"};
                case 3: return h::kZoomIn;
                case 4: return {"Next demodulation mode: WFM, NFM, AM, USB, LSB, CW",
                                 "Modo di demodulazione successivo: WFM, NFM, AM, USB, LSB, CW",
                                 "Mode de démodulation suivant : WFM, NFM, AM, USB, LSB, CW",
                                 "Nächste Demodulationsart: WFM, NFM, AM, USB, LSB, CW",
                                 "Modo de demodulación siguiente: WFM, NFM, AM, USB, LSB, CW",
                                 "次の復調モード：WFM、NFM、AM、USB、LSB、CW", "下一个解调模式：WFM、NFM、AM、USB、LSB、CW"};
            }
            break;
        case Page::Visual:
            switch (slot) {
                case 1: return h::kTheme;
                case 2: return {"Show or hide the frequency grid",
                                 "Mostra o nascondi la griglia delle frequenze",
                                 "Affiche ou masque la grille des fréquences",
                                 "Blendet das Frequenzraster ein oder aus",
                                 "Muestra u oculta la cuadrícula de frecuencias",
                                 "周波数グリッドを表示／非表示", "显示或隐藏频率网格"};
                case 3: return {"Change how the screen splits the waterfall and the spectrum",
                                 "Cambia la divisione dello schermo tra waterfall e spettro",
                                 "Change le partage de l’écran entre cascade et spectre",
                                 "Ändert die Aufteilung zwischen Wasserfall und Spektrum",
                                 "Cambia el reparto de la pantalla entre cascada y espectro",
                                 "ウォーターフォールとスペクトラムの画面配分を変更", "更改瀑布图与频谱的屏幕分配"};
                case 4: return {"Hold the spectrum peaks, or stop holding them",
                                 "Mantieni i picchi dello spettro, o smetti di mantenerli",
                                 "Maintient les crêtes du spectre, ou arrête de les maintenir",
                                 "Hält die Spektrumspitzen fest oder gibt sie frei",
                                 "Retiene los picos del espectro, o deja de retenerlos",
                                 "スペクトラムのピークホールドをオン／オフ", "开启或关闭频谱峰值保持"};
            }
            break;
        case Page::Audio:
            switch (slot) {
                case 1: return {"Mute or unmute the audio", "Silenzia o riattiva l’audio",
                                 "Coupe ou rétablit le son", "Schaltet den Ton stumm oder wieder ein",
                                 "Silencia o reactiva el audio",
                                 "音声をミュート／解除", "静音或取消静音"};
                case 2: return {"Volume down", "Abbassa il volume", "Baisse le volume",
                                 "Leiser", "Baja el volumen",
                                 "音量を下げる", "降低音量"};
                case 3: return {"Volume up", "Alza il volume", "Monte le volume",
                                 "Lauter", "Sube el volumen",
                                 "音量を上げる", "提高音量"};
                case 4: return {"The current volume. This key does nothing.",
                                 "Il volume attuale. Questo tasto non fa nulla.",
                                 "Le volume actuel. Cette touche n’a pas d’effet.",
                                 "Die aktuelle Lautstärke. Diese Taste hat keine Funktion.",
                                 "El volumen actual. Esta tecla no hace nada.",
                                 "現在の音量。このキーは使いません。", "当前音量。此键无功能。"};
            }
            break;
        case Page::Settings:
            switch (slot) {
                case 1: return {"Lower the tuner gain", "Riduci il guadagno del tuner",
                                 "Baisse le gain du tuner", "Senkt die Tuner-Verstärkung",
                                 "Baja la ganancia del sintonizador",
                                 "チューナーのゲインを下げる", "降低调谐器增益"};
                case 2: return {"Raise the tuner gain", "Aumenta il guadagno del tuner",
                                 "Monte le gain du tuner", "Erhöht die Tuner-Verstärkung",
                                 "Sube la ganancia del sintonizador",
                                 "チューナーのゲインを上げる", "提高调谐器增益"};
                case 3: return {"Switch the automatic gain on or off",
                                 "Attiva o disattiva il guadagno automatico",
                                 "Active ou désactive le gain automatique",
                                 "Schaltet die automatische Verstärkung ein oder aus",
                                 "Activa o desactiva la ganancia automática",
                                 "自動ゲインをオン／オフ", "开启或关闭自动增益"};
                case 4: return h::kBackToMenu;
            }
            break;
    }
    return {};
}

// --- publish helpers ---
void SdrViewModel::publish_vfo() {
    char buffer[16];
    const int32_t centre = model_.vfo_hz();
    const int32_t span_hz = model_.span_khz() * 1000;
    format_vfo(buffer, sizeof(buffer), centre);
    vfo_text_subject_.set(buffer);

    int64_t low = static_cast<int64_t>(centre) - span_hz / 2;
    if (low < 0) low = 0;
    format_freq_plain(buffer, sizeof(buffer), static_cast<int32_t>(low));
    band_low_text_subject_.set(buffer);

    int64_t high = static_cast<int64_t>(centre) + span_hz / 2;
    if (high > INT32_MAX) high = INT32_MAX;
    format_freq_plain(buffer, sizeof(buffer), static_cast<int32_t>(high));
    band_high_text_subject_.set(buffer);

    int64_t q1 = static_cast<int64_t>(centre) - span_hz / 4;
    if (q1 < 0) q1 = 0;
    format_freq_plain(buffer, sizeof(buffer), static_cast<int32_t>(q1));
    grid_low_text_subject_.set(buffer);

    int64_t q3 = static_cast<int64_t>(centre) + span_hz / 4;
    if (q3 > INT32_MAX) q3 = INT32_MAX;
    format_freq_plain(buffer, sizeof(buffer), static_cast<int32_t>(q3));
    grid_high_text_subject_.set(buffer);
}

void SdrViewModel::publish_mode() {
    mode_text_subject_.set(radio_mode_name(model_.mode()));
}

void SdrViewModel::publish_grids() {
    freq_grid_subject_.set(model_.freq_grid());
    time_grid_subject_.set(model_.time_grid());
}

void SdrViewModel::publish_audio() {
    muted_subject_.set(model_.muted());
    volume_subject_.set(model_.volume());
}

void SdrViewModel::publish_gain() {
    char buf[8];
    if (model_.gain_auto()) {
        std::snprintf(buf, sizeof(buf), "auto");
    } else {
        std::snprintf(buf, sizeof(buf), "%dd", static_cast<int>(model_.gain_tenth_db() / 10));
    }
    gain_text_subject_.set(buf);
}

} // namespace sdr
