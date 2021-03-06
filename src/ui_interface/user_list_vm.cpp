#include "user_list_vm.hpp"
#include "../github/client.hpp"
#include "../sqlite/sqlite.hpp"
#include <iostream>
#include <thread>
using mx3_gen::UserListVmObserver;
using mx3_gen::UserListVmCell;

namespace chrono {
    using namespace std::chrono;
}

namespace {
    const string s_count_stmt { "SELECT COUNT(1) FROM `github_users` ORDER BY id;" };
    const string s_list_stmt  { "SELECT `login` FROM `github_users` ORDER BY id;" };
}

namespace mx3 {

UserListVm::UserListVm(shared_ptr<sqlite::Db> db)
    : m_count{nullopt}
    , m_cursor_pos {0}
    , m_db {db}
    , m_count_stmt { m_db->prepare(s_count_stmt) }
    , m_list_stmt  { m_db->prepare(s_list_stmt) }
    , m_query { m_list_stmt->exec_query() }
{}

int32_t
UserListVm::count() {
    if (m_count) {
        return *m_count;
    }
    auto start = chrono::steady_clock::now();
    m_count = m_count_stmt->exec_query().int_value(0);
    auto end = chrono::steady_clock::now();
    double millis = chrono::duration_cast<chrono::nanoseconds>( end - start ).count() / 1000000.0;
    std::cout << "Count: " << *m_count << " (" << millis << ") milliseconds" << std::endl;
    return *m_count;
}

optional<UserListVmCell>
UserListVm::get(int32_t index) {
    auto start = chrono::steady_clock::now();

    if (index < static_cast<int32_t>(m_row_cache.size())) {
        return m_row_cache[index];
    }

    m_row_cache.resize(index + 1);
    while (m_query.is_valid() && m_cursor_pos <= index) {
        m_row_cache[m_cursor_pos] = UserListVmCell {m_cursor_pos, m_query.string_value(0)};
        m_query.next();
        m_cursor_pos++;
    }

    auto end = chrono::steady_clock::now();

    if (m_cursor_pos == index + 1) {
        double millis = chrono::duration_cast<chrono::nanoseconds>( end - start ).count() / 1000000.0;
        std::cout << millis << " milliseconds" << std::endl;
        return *(m_row_cache[index]);
    } else {
        return nullopt;
    }
}

UserListVmHandle::UserListVmHandle(
    shared_ptr<mx3::sqlite::Db> db,
    const mx3::Http& http,
    mx3::EventLoopRef ui_thread
)
    : m_db(db)
    , m_http(http)
    , m_stop(false)
    , m_observer(nullptr)
    , m_ui_thread(std::move(ui_thread)) {}

void
UserListVmHandle::start(const shared_ptr<UserListVmObserver>& observer) {
    auto db = m_db;
    auto ui_thread = m_ui_thread;

    github::get_users(m_http, nullopt, [db, ui_thread, observer] (vector<github::User> users) mutable {
        auto update_stmt = db->prepare("UPDATE `github_users` SET `login` = ?2 WHERE `id` = ?1;");
        auto insert_stmt = db->prepare("INSERT INTO `github_users` (`id`, `login`) VALUES (?1, ?2);");
        sqlite::TransactionStmts transaction_stmts {db};
        sqlite::TransactionGuard guard {transaction_stmts};

        for (const auto& user : users) {
            update_stmt->reset();
            update_stmt->bind(1, user.id);
            update_stmt->bind(2, user.login);
            if ( update_stmt->exec() == 0 ) {
                insert_stmt->reset();
                insert_stmt->bind(1, user.id);
                insert_stmt->bind(2, user.login);
                insert_stmt->exec();
            }
        }
        guard.commit();
        ui_thread.post([db, observer] () {
            // todo(kabbes) make sure to check if this has been stopped
            observer->on_update( make_shared<UserListVm>(db) );
        });
    });
}

void
UserListVmHandle::stop() {
    // this isn't implemented yet :(
}

}
