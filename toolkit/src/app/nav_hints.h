/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "i18n.h"

// Key tooltip texts that several apps share (see NavProvider::nav_hint).
// Order in each entry: en, it, fr, de, es.
namespace toolkit::hints {

inline constexpr i18n::Text kZoomIn{
    "Zoom in", "Ingrandisci", "Zoom avant", "Vergrößern", "Acercar"};
inline constexpr i18n::Text kZoomOut{
    "Zoom out", "Riduci lo zoom", "Zoom arrière", "Verkleinern", "Alejar"};
inline constexpr i18n::Text kTheme{
    "Switch between the light and dark theme",
    "Alterna il tema chiaro e quello scuro",
    "Alterne entre le thème clair et le thème sombre",
    "Wechselt zwischen hellem und dunklem Design",
    "Alterna entre el tema claro y el oscuro"};
inline constexpr i18n::Text kListUp{
    "Move up the list", "Sali nella lista", "Monte dans la liste",
    "In der Liste nach oben", "Sube en la lista"};
inline constexpr i18n::Text kListDown{
    "Move down the list", "Scendi nella lista", "Descend dans la liste",
    "In der Liste nach unten", "Baja en la lista"};
inline constexpr i18n::Text kSort{
    "Change the sort order", "Cambia l’ordinamento", "Change l’ordre de tri",
    "Ändert die Sortierung", "Cambia el orden"};
inline constexpr i18n::Text kTracks{
    "Show or hide the tracks", "Mostra o nascondi le tracce", "Affiche ou masque les traces",
    "Blendet die Spuren ein oder aus", "Muestra u oculta las estelas"};
inline constexpr i18n::Text kRadarMap{
    "Switch between the radar and the map", "Alterna il radar e la mappa",
    "Alterne entre le radar et la carte", "Wechselt zwischen Radar und Karte",
    "Alterna entre el radar y el mapa"};
inline constexpr i18n::Text kSettingPrev{
    "Previous setting", "Impostazione precedente", "Réglage précédent",
    "Vorherige Einstellung", "Ajuste anterior"};
inline constexpr i18n::Text kSettingNext{
    "Next setting", "Impostazione successiva", "Réglage suivant",
    "Nächste Einstellung", "Ajuste siguiente"};
inline constexpr i18n::Text kSettingChange{
    "Change the value of the setting", "Cambia il valore dell’impostazione",
    "Change la valeur du réglage", "Ändert den Wert der Einstellung",
    "Cambia el valor del ajuste"};
inline constexpr i18n::Text kBackToMenu{
    "Close this app and go back to the ZeroRadio menu",
    "Chiudi questa app e torna al menu di ZeroRadio",
    "Ferme cette app et revient au menu ZeroRadio",
    "Schließt diese App und kehrt zum ZeroRadio-Menü zurück",
    "Cierra esta app y vuelve al menú de ZeroRadio"};

} // namespace toolkit::hints
