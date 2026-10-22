/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "memories_store.h"
#include "nav_provider.h"
#include "shell_viewmodel.h"
#include "subjects.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace memories {

class MemoriesViewModel : public toolkit::ShellViewModel, public toolkit::NavProvider {
public:
    struct Row {
        bool group = false;
        std::string name;
        std::string frequency;
        std::string mode;
        size_t memory_index = 0;
        bool marked = false;
    };

    MemoriesViewModel();

    const std::vector<Row>& rows() const { return rows_; }
    int selected_row() const { return selected_row_.value(); }
    const std::string& query() const { return query_; }
    bool sort_by_frequency() const { return sort_by_frequency_; }
    lv_subject_t* rows_version_subject() { return rows_version_.native(); }
    lv_subject_t* selected_row_subject() { return selected_row_.native(); }
    lv_subject_t* search_request_subject() { return search_request_.native(); }
    lv_subject_t* scan_request_subject() { return scan_request_.native(); }
    lv_subject_t* status_subject() { return status_.native(); }
    size_t scan_count() const { return pending_scan_.size(); }
    size_t scan_total() const { return pending_scan_total_; }
    bool scan_uses_marks() const { return scan_uses_marks_; }

    void set_query(std::string query);
    void move_selection(int direction);
    void open_selected_in_sdr();
    void toggle_sort();
    void request_search();
    void toggle_mark();
    void clear_marks();
    void prepare_scan(bool force_group = false);
    void confirm_scan();
    void cancel_scan();

    // --- EDIT page ---
    // What the edit dialog starts from: the selected memory when it opened.
    struct EditDraft {
        int64_t hz = 0;
        std::string name;
        std::string mode;
        std::string group;
    };
    const EditDraft& edit_draft() const { return edit_draft_; }
    lv_subject_t* edit_request_subject() { return edit_request_.native(); }
    // Open the edit dialog on the selected memory (the view observes the
    // request), then apply its name and group. The group is the first tag:
    // an empty one drops it, the other tags and the note stay.
    void request_edit();
    void apply_edit(const std::string& name, const std::string& group);
    // The selected memory's next mode: WFM, NFM, AM, USB, LSB, CW.
    void cycle_mode();
    // Delete the selected memory on a second press within kDeleteConfirmMs
    // of the first. Moving the cursor, TAB, search, edit, a page switch or
    // another NavBar key disarms it; so does tick() once the time is up.
    static constexpr int64_t kDeleteConfirmMs = 4000;
    void request_delete(int64_t now_ms);
    bool delete_armed() const { return delete_armed_; }
    void tick(int64_t now_ms); // the view calls it a few times a second
    static int64_t now_ms();   // the clock request_delete() and tick() use
    void nav_page_changed(int page) override { (void)page; disarm_delete(); }

    int nav_page_count() const override { return 3; }
    void nav_fill(int page, NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;
    const char* nav_page_name(int page) const override;
    toolkit::i18n::Text nav_hint(int page, int slot) const override;

    void on_up() override { move_selection(-1); }
    void on_down() override { move_selection(1); }
    void on_enter() override { open_selected_in_sdr(); }
    void on_tab() override { toggle_sort(); }

private:
    void rebuild_rows();
    const toolkit::Memory* selected_memory() const;
    // Reload the shared file after a change, keep the scan marks, and put the
    // cursor on (hz, name), or on the memory row nearest `fallback_row`.
    // `old_hz`/`old_name`: the identity the memory had before a rename, so
    // its scan mark follows it.
    void reload(int64_t hz, const std::string& name, int fallback_row, int64_t old_hz = 0,
                const std::string& old_name = {});
    void disarm_delete();
    void update_status();
    int first_memory_row() const;
    static std::string format_frequency(int64_t hz);

    std::vector<toolkit::Memory> memories_;
    std::vector<bool> marked_;
    std::vector<Row> rows_;
    std::vector<size_t> pending_scan_;
    size_t marked_count_ = 0;
    size_t pending_scan_total_ = 0;
    bool scan_uses_marks_ = false;
    std::string query_;
    bool sort_by_frequency_ = false;
    reactive::IntSubject rows_version_{0};
    reactive::IntSubject selected_row_{-1};
    reactive::IntSubject search_request_{0};
    reactive::IntSubject scan_request_{0};
    reactive::StringSubject<64> status_{""};
    reactive::IntSubject edit_request_{0};
    EditDraft edit_draft_;
    bool delete_armed_ = false;
    int64_t delete_hz_ = 0;
    std::string delete_name_;
    int64_t delete_armed_at_ = 0;
    mutable std::string mode_label_; // EDIT page key 6, see nav_fill()
};

} // namespace memories
