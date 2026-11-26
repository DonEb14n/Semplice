-- Migration 0004: an artist the owner hid by hand (Edit artist -> Status in Library -> Hide).
-- Not the same as artist.hidden (that one means "no folder, only a credit"). A user-hidden artist keeps
-- its folder and albums; the Library Artists tab and the Home discography carousels leave it out.

ALTER TABLE artist ADD COLUMN user_hidden INTEGER NOT NULL DEFAULT 0 CHECK (user_hidden IN (0,1));
