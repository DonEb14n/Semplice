-- Migration 0003: "tags out of sync" marker (SPEC 6.2).
-- Set inside the transaction of every operation that changes what a track's file tags must say;
-- cleared after the tags were written successfully. A locked file or a crash leaves it set and
-- the next Rescan retries.

ALTER TABLE track ADD COLUMN tags_dirty INTEGER NOT NULL DEFAULT 0 CHECK (tags_dirty IN (0,1));
CREATE INDEX ix_track_tags_dirty ON track(tags_dirty) WHERE tags_dirty = 1;
