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

    int nav_page_count() const override { return 2; }
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
};

} // namespace memories
