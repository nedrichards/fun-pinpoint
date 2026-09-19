# Pinpoint

Pinpoint is a presentation application for hackers. This is a
fidelity-first rebuild of the [original Clutter-based Pinpoint](https://gitlab.gnome.org/Archive/pinpoint): existing `.pin`
presentations remain useful, while the application gains a modern setup screen,
source editor, speaker view, rehearsal workflow, and PDF export by being targeted as a GTK 4 and libadwaita presentation application for GNOME.

This work is inspired by and dedicated to my former colleagues at the [Intel 
Open Source Technology Center](https://desktopsummit.org/sponsors/intel.html) 
in London. We tried to fuse creative technologists and designers together to 
build extraordinary software that took ease of use, ease of hacking and easy on
 the eye to the next level. Like many things we were probably more influential 
than successful but pinpoint was software we built for us. To share what we 
were up to and in frustration that all the software we had available made 
creating good presentations so hard, especially for non designers. 
Whilst you can quickly create some mediocre slides in pinpoint it does make 
it hard to create something truly awful. I've kept using it ever since, but 
over time it's been harder to keep up to date as the world has moved on and 
the concepts we worked on became mainstream. These projects are about bringing 
this tool I use into the modern era, firstly from upgrading the tech stack to 
 GTK4. And then subsequently in the looser 0.3.0 series in a different repo 
changing the implementation language to Rust. The codenames of these 
repositories are based on in jokes from the team about terrible names for forks 
of open source code and are meant with love.

## Using Pinpoint

The setup screen provides three starting points:

- **Present from Folder…** opens a folder portal so the presentation and its
  relative assets remain available to the sandboxed application.
- **New Presentation** creates a source-first presentation in the composition
  editor.
- The bundled **Introduction, Made with Pinpoint** can be viewed, saved, or
  copied into the editor as a starting point.

After selecting a deck, its card shows a parsed slide and asset summary, then
offers Present and Rehearse alongside quieter Edit and PDF export actions.

The composition editor uses the same parser and renderer as presentation mode.
It provides Pinpoint-aware syntax highlighting and completion, inline source
diagnostics, a live safe preview, explicit saving with external-change
protection, and integrated rehearsal. Present opens the full delivery view;
Rehearse keeps the editor open and places the speaker view beside it so source
and timings can be adjusted together. See
[docs/composition-editor.md](docs/composition-editor.md).

Pinpoint also follows files saved by external editors. Reloading, diagnostics,
and editor integration are described in
[docs/external-editors.md](docs/external-editors.md).

## Presentation format

Presentations are UTF-8 text files, conventionally named with a `.pin` suffix.
Slides are separated by a line beginning with three or more hyphens:

```text
[font=Sans 48px]
[duration=20]

# Welcome

---
[bgcolor=#204060]

This is the second slide.
```

The format supports styled text, images, SVG, video, camera input, transitions,
speaker notes, slide durations, and source-relative assets. The complete syntax
reference is [docs/presentation-format.md](docs/presentation-format.md).

## Presentation controls

| Input | Action |
| --- | --- |
| Right, Down, Page Down, Space | Next slide |
| Left, Up, Backspace, Page Up | Previous slide |
| Home or `H` | First slide |
| `F` or F11 | Toggle fullscreen |
| F1 | Toggle speaker view |
| `B` | Toggle screen blanking |
| Return / Tab | Run / edit the slide command |
| `S` in speaker view | Swap audience and speaker displays |
| Escape or `Q` | Return to the editor, or quit Pinpoint |

Touchscreen taps advance slides and briefly reveal the presentation's close
control. Pointer movement reveals the same control without advancing, while
horizontal swipes navigate slides. Standard MPRIS clients can use Next and
Previous; Pinpoint deliberately rejects media playback, seek, and volume
operations because they do not truthfully describe a presentation. Speaker
view can run in another window or on a second monitor and shows the current
slide, next slide, notes, and timing information.

## Command line

Run a deck directly with:

```sh
flatpak run --user com.nedrichards.pinpoint talk.pin
```

Use `flatpak run --user com.nedrichards.pinpoint --edit [talk.pin]` to open
composition mode, or append `--help` for presentation, rehearsal, validation,
camera, and PDF-export options. The stable command-line contract is documented in
[docs/command-line.md](docs/command-line.md).

## Development

Pinpoint is built and tested with the pinned GNOME 51 SDK. Install the
development Flatpak before running the test suite so media decoding has the
application's Flatpak identity. The exact configure, build, test, analysis, and
local-run commands are in [docs/development.md](docs/development.md). The
documentation index in
[docs/README.md](docs/README.md) links the format, architecture, compatibility,
accessibility, performance, and validation references without repeating them
here.

## License

Pinpoint is distributed under the GNU Lesser General Public License, version
2.1 or later. See [COPYING](COPYING). Provenance and licensing for the bundled
introduction media are recorded in
[data/introduction/ORIGIN.md](data/introduction/ORIGIN.md).
