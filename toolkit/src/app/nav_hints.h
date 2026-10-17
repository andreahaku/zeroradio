/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "i18n.h"

// Key tooltip texts that several apps share (see NavProvider::nav_hint).
// Order in each entry: en, it, fr, de, es, ja, zh.
namespace toolkit::hints {

inline constexpr i18n::Text kZoomIn{
    "Zoom in", "Ingrandisci", "Zoom avant", "Vergrößern", "Acercar",
    "ズームイン", "放大"};
inline constexpr i18n::Text kZoomOut{
    "Zoom out", "Riduci lo zoom", "Zoom arrière", "Verkleinern", "Alejar",
    "ズームアウト", "缩小"};
inline constexpr i18n::Text kTheme{
    "Switch between the light and dark theme",
    "Alterna il tema chiaro e quello scuro",
    "Alterne entre le thème clair et le thème sombre",
    "Wechselt zwischen hellem und dunklem Design",
    "Alterna entre el tema claro y el oscuro",
    "ライトテーマとダークテーマを切り替え", "切换浅色和深色主题"};
inline constexpr i18n::Text kListUp{
    "Move up the list", "Sali nella lista", "Monte dans la liste",
    "In der Liste nach oben", "Sube en la lista",
    "リストを上へ移動", "在列表中上移"};
inline constexpr i18n::Text kListDown{
    "Move down the list", "Scendi nella lista", "Descend dans la liste",
    "In der Liste nach unten", "Baja en la lista",
    "リストを下へ移動", "在列表中下移"};
inline constexpr i18n::Text kSort{
    "Change the sort order", "Cambia l’ordinamento", "Change l’ordre de tri",
    "Ändert die Sortierung", "Cambia el orden",
    "並び順を変更", "更改排序方式"};
inline constexpr i18n::Text kTracks{
    "Show or hide the tracks", "Mostra o nascondi le tracce", "Affiche ou masque les traces",
    "Blendet die Spuren ein oder aus", "Muestra u oculta las estelas",
    "航跡を表示／非表示", "显示或隐藏航迹"};
inline constexpr i18n::Text kRadarMap{
    "Tap: radar or map. Hold on the map: follow the selected contact on or off.",
    "Tocca: radar o mappa. Tieni premuto sulla mappa: segui il contatto selezionato, o smetti.",
    "Appui : radar ou carte. Maintien sur la carte : suivre le contact choisi, ou arrêter.",
    "Tippen: Radar oder Karte. Halten auf der Karte: dem gewählten Kontakt folgen oder nicht.",
    "Pulsa: radar o mapa. Mantén en el mapa: seguir el contacto elegido, o dejar de seguirlo.",
    "短押し：レーダー／地図。地図で長押し：選択中の目標の追従をオン／オフ。",
    "轻按：雷达或地图。在地图上长按：开启或关闭跟随所选目标。"};
inline constexpr i18n::Text kSettingPrev{
    "Previous setting", "Impostazione precedente", "Réglage précédent",
    "Vorherige Einstellung", "Ajuste anterior",
    "前の設定", "上一项设置"};
inline constexpr i18n::Text kSettingNext{
    "Next setting", "Impostazione successiva", "Réglage suivant",
    "Nächste Einstellung", "Ajuste siguiente",
    "次の設定", "下一项设置"};
inline constexpr i18n::Text kSettingChange{
    "Change the value of the setting", "Cambia il valore dell’impostazione",
    "Change la valeur du réglage", "Ändert den Wert der Einstellung",
    "Cambia el valor del ajuste",
    "設定の値を変更", "更改该设置的值"};
inline constexpr i18n::Text kBackToMenu{
    "Close this app and go back to the ZeroRadio menu",
    "Chiudi questa app e torna al menu di ZeroRadio",
    "Ferme cette app et revient au menu ZeroRadio",
    "Schließt diese App und kehrt zum ZeroRadio-Menü zurück",
    "Cierra esta app y vuelve al menú de ZeroRadio",
    "このアプリを閉じて ZeroRadio のメニューに戻る", "关闭此应用并返回 ZeroRadio 菜单"};

} // namespace toolkit::hints
