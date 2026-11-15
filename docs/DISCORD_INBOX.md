# Collecting share packs on Discord

Vitals never sends anything. A player who ticked **Yes, help improve Vitals** gets `Vitals-Share.zip` on their PC and
chooses whether to attach it in the Vitals Discord server. This page is for the person running that server.

## Server setup (once)

1. Create a **private ticket category**, for example `Share packs`. Players must not be able to read each other's
   channels: only the player who opened it and the maintainers can see a ticket channel. A free ticket bot (such as
   Ticket Tool) does this: one button, one private channel per player, files allowed.
2. Pin the player instructions in the welcome channel: open a ticket, drag `Vitals-Share.zip` in, say nothing else.
   Packs are already scrubbed (user name, PC name, emails, IPs and secrets removed) and are small.
3. Say in the server rules what happens to a pack: only the maintainers read it, it is used to find broken mods, and
   the player can ask for it to be deleted by closing the ticket and asking.

## Letting Claude (or you) pull many packs

`tools/share_inbox.py pull-discord` downloads every `.zip` attachment from the ticket channels into a folder. It uses a
**read-only bot** that you create and keep; its token is never shared with anyone, never put in a chat, and never
committed.

1. Discord Developer Portal, New Application, **Bot**, Reset Token (copy it once). Leave all "Privileged Gateway
   Intents" off: the script only needs the REST API.
2. Invite the bot to the server with only these permissions: **View Channels** and **Read Message History**. Add it to
   the ticket category so it can see the private channels.
3. Developer Mode on in Discord (Settings, Advanced), then right-click the server for its id and the ticket category for
   its id.
4. On your PC, in a terminal (the token only lives in this window):

   ```
   set VITALS_DISCORD_TOKEN=<token>
   set VITALS_DISCORD_GUILD=<server id>
   set VITALS_DISCORD_CATEGORY=<ticket category id>
   python tools\share_inbox.py pull-discord --out inbox
   python tools\share_inbox.py summary inbox --out summary.md
   ```

   A second run fetches only new files (names are `<message id>_<attachment id>-<name>`). Files that are not Vitals
   packs are ignored by `summary`.
5. Put `inbox` and `summary.md` in the project's shared folder. Claude reads the summary first (counts and file names
   only) and opens single packs only to check a candidate rule.

Rules that stay as before: raw packs never go in git or to a third-party AI service, a rule needs at least 5 distinct
installs, and a person tests it before it ships.

## The old Cloudflare page

Early builds used a Cloudflare upload page (Worker, R2 bucket, KV namespace). It is retired: nothing points at it any
more and its code is gone from the repository. If you still have it deployed, delete the Worker, the R2 bucket and the
KV namespace in the Cloudflare dashboard.
