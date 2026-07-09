# Qt Camera Project with UDP

**Turn a handful of Raspberry Pi's into a face-aware surveillance system.**

Point the Pi cameras at whatever you want watched. They stream video over your Wi-Fi to a central Windows app that stitches every feed into one live wall — up to four cameras at once. The app watches for motion, records automatically when something happens, and recognizes the faces it's seen before.

The best part comes later. When you play back a recording, you can pick a person from a dropdown and jump straight to every moment they appeared on camera. No scrubbing. No hunting. Click a name, click next, there they are.

---

## What it does

🎥 **Live multi-camera wall.** Up to four Pi cameras on the same Wi-Fi stream in real time, side by side.

🧠 **Face recognition.** Add someone to the system once, and every camera knows them from then on. Names float over faces as they walk past.

🟢 **Smart recording.** A little green dot lights up when there's motion. Recording starts on its own, keeps going while the action lasts, and stops when things go quiet. You can also hit record manually whenever you want.

⏪ **Face-jump playback.** This is the one. Pick a person, hit next, and you land on the exact moment they walked into frame. Every appearance is a click away.

🕒 **Live clock overlay** on every feed, so timestamps in playback actually mean something.

---

## Screenshots

> _Add real shots here once you have them — grid view, face recognition in action, playback with the face-jump bar._

![Main grid](docs/screenshots/main-grid.png)
![Face recognition](docs/screenshots/face-recognition.png)
![Playback with face-jump](docs/screenshots/playback.png)

---

## How it works, roughly

- Raspberry Pi cameras encode video and beam it over Wi-Fi to the Windows machine.
- The desktop app catches all four streams, displays them in a grid, and runs face recognition on whatever's currently showing.
- When something moves, that camera starts saving frames to disk. When things calm down, it stops on its own.
- Every face the app recognizes gets logged with a timestamp — that's what makes the face-jump feature possible.

Built with **Qt 6** and **OpenCV** (YuNet + SFace). The heavy lifting runs on a background thread so all four cameras stay smooth.

---

## Quick start

You'll need Qt 6, OpenCV 4.8+, and MSVC 2022 on Windows. Open `qlabel_vers.pro` in Qt Creator, build in Release mode, drop the two ONNX model files into `C:/kamera_proje/models/`, and run.

For the Pi side, any script that grabs a frame from the camera and fires it off as a UDP packet to ports 5000–5003 works — a dozen lines of Python is enough.

---

## Adding a face

Turn on face recognition, click **Enroll**, get in front of a camera, type a name. Done. That person will be recognized on any camera from that point on. Use **Manage** later to rename or remove people.

---

## About

Built during my internship at **Turkish Technic**.

_This is a portfolio / learning project, not a commercial product._
