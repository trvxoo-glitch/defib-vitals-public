# Collecting share packs on Discord

Vitals never sends anything. A player who ticked **Yes, help improve Vitals** gets `Vitals-Share.zip` on their PC and
chooses whether to attach it in the Vitals Discord server. This page is for the person running that server.

## Server setup (once)

1. Create a **private ticket category**, for example `Share packs`. Players must not be able to read each other's
   channels: only the player who opened it and the maintainers can see a ticket channel. A free ticket bot (such as
   Ticket Tool) does this: one button, one private channel per player, files allowed.
2. Pin the player instructions in the welcome channel: press **Upload**, drag `Vitals-Share.zip` into the private channel that opens, say nothing else.
   Packs are already scrubbed (user name, PC name, emails, IPs and secrets removed) and are small.
3. Say in the server rules what happens to a pack: only the maintainers read it, it is used to find broken mods, and
   the player can ask for it to be deleted by closing the ticket and asking.

## Pulling many packs

`tools/share_inbox.py pull-discord` downloads every `.zip` attachment from the ticket channels into a folder. It uses a
**read-only bot** that you create and keep; its token is never shared with anyone, never put in a chat, and never
committed.

1. Discord Developer Portal, New Application, **Bot**, Reset Token (copy it once). Under "Privileged Gateway
   Intents" turn on **Message Content Intent** and save: without it Discord hides every attachment from the bot (it
   reads the messages but sees no files). The other two stay off. The script only uses the REST API and never
   connects as an online bot.
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
   packs are ignored by `summary`. The category id is required: the tool refuses to run without it rather than read
   every channel of the server. The bot token is sent only to Discord's API, never to the attachment download links (which are fetched only from
   `cdn.discordapp.com` or `media.discordapp.net`),
   and `summary` skips any zip that unpacks to more than a Vitals pack can (16 MB per file, 64 MB in all).
5. Keep `inbox` (the raw packs) on your own PC, in no folder that an AI assistant or any other service can read. Only
   `summary.md` (counts and file names) may go in a shared folder. An AI assistant sees the summary; **a person** opens a
   pack when one has to be read to check a candidate rule. This is what the privacy page promises.

Rules that stay as before: raw packs never go in git or to a third-party AI service or any other person or service, a rule needs at least 5 distinct
installs, and a person tests it before it ships.

## The old Cloudflare page

Early builds used a Cloudflare upload page (Worker, R2 bucket, KV namespace). It is retired: nothing points at it any
more and its code is gone from the repository. If you still have it deployed, delete the Worker, the R2 bucket and the
KV namespace in the Cloudflare dashboard.
