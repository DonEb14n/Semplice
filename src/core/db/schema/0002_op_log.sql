-- Migration 0002: commit marker for journaled file operations (SPEC 4.5).
-- The database transaction of an operation inserts its uuid here; after a crash the journal
-- recovery asks "is this uuid present?" to decide between rolling forward and rolling back.
-- Rows are removed when the operation has finished (or recovery has completed it).

CREATE TABLE op_log (
    op_id        TEXT PRIMARY KEY,
    committed_at INTEGER NOT NULL
);
