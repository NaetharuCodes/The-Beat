# The Beat

The Beat is an audio-visual app written in C++ and using OpenGL. It allows users to create beautiful particle based visuals that will
sync with music.

## How does it work!?

At its core The Beat uses Curl Noise - a variation on Perlin noise which creates a random noise landscape in 3D. This produces vectors
that can then be used to guide particles around the screen, creating fluid like simulations. This was heavily based on the work of
Daniel Shiffman and his book "The Nature of Code".

The controls focus on manipulating the noise field, rather than the particles themselves. Allowing them to move in a natural way and
ensuring interesting configurations arise naturally, emerging from their behaviors over the noise.

The particle based computations are all handled via OpenGL on the computer shaders, which allows for highly efficient managing of the
particles and makes very large parcile numbers possible. Running on an RTX5090 GPU, the app will comfortably simulate upward of 50 million
particles at once.

So to summerize:

1. We have perlin noise in 3D. A 2D slice of that is user to get a field of vectors (converted to curl noise). Over time we push into the
   z axis of that noise getting new "slices" which results in a smooth evolution of the noise pattern.

2. We have a basic physics engine on the particles. They move, have attraction, and respond to the vectors with inertia. Allowing them to
   move around the screen in a way that looks and feels natural.

3. We allow the user to adjust many of the key perameters of the field, such as adjusting the magnitude of the vectors, the scale of the noise,
   and the speed at which we move through the z-axis.

## Music!

We also have an audio engine. This is using MiniAudio. We allow the user to load up an audio source (mp3) and then while it plays we sample that audio.
Via the audio engine we break the audio down into beats, bass, mids and highs. From there we sample each of these against the local norm. We keep a small
window to determine the "normal" level which allows us to only react to beats that spike above that normal level beyond a given threshold
which can be adjusted by the sensativity controls.

We then map these moments to different visual effects. The pattern of beats can be used to move (add small random vectors) to particles, and to
add short bursts of speed. Other events are mapped to other features, such as hue rotation and point size. The bass point size needs work and is perhaps
best left off at this point as it tends to just result in a stable size increase unless the track in question as a very staccato bass.

## Pictures!

We also have the ability to map particles to images, shapes, and even text. This works by first taking the sample image / shape and doing a
frequency distribution over it, where each pixel is assigned a range in our distribution relative to its brightness. Bright pixels get the larges
range while dark the smallest. We add a small floor to this so that we get a little noise even in the darkest areas. We then pass this into the GPU
and the pixels are sent to the appropriate locations, randomly distributed across the target based on its frequency. And ths we "generate" the
chosen image / shape / text. This remains iteracterbale and responseive to other preameters while active, so the user can click on the shape to
use mouse interactions, or use music response to have the shape respond with motion / etc.

## Sequencer!

We can also assign the shapes as per the above to time stamps in the playback. This means we can create something like music videos with timed
interactions that take place in real time. Words that map to key lyrics can fade in and out. Images appropriate to the theme can be formed and then fade
back into the flow. Each entry offers a time to come in, a time to release (measured in seconds) and also a ramp up and down time. These impact how
quickly the particles will move, allowing for slow smooth formation or quick snappy ones as desired.

Anyhow, that is all for now. Please feel free to download, and play with the software. I'll also compile and host a version of it on my own website
if you would prefer to just download the exe.
