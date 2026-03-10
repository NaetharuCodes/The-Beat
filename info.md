WHAT IS THE BEAT?

The Beat is an app that uses a physics based particle system to create patterns and images in time to the beat of music. It is to be used as part of live music productions and as a visualizer for pre-recorded audio.

A large number of particles are rendered into the screen space.

The user loads up images in the form of PNG and JPG files.

The user connects the app to an audio source (can be an mp3 file, or an audio in via a DAC).

The user then has a control panel of knobs, dials, and other controls they can adjust to impact the visual style. These all effect aspects of the particle system such as if they have attraction or repulsion of each other, what size and colors they are, etc.

The user then plays the audio can sequence the images (when they load and unload in seconds into the play). The particles move around using the perlin noise field and when an image loads they move to make that image. The image itself is never displayed. But is used to take the brightness values from the pixels and arrange the particles into that arrangement showing the image in monochrome via the particles with more clumping up in the brigher areas, and fewer in the darker ones.

Then after the image changes or unloads the particles move on, either to fly into a new image, or to go back into making beautiful patterns to the beat.

I suggest we use the same 3D perlin noise field with variable controls as part of the particle system here, and may want to consider other physics properties and springs on them to give them cool motion. They should be smooth moving and cool looking.

When not making images they should have a range of things we can control so they make amazing patterns in time with the beat, by connecting various controls for the particles to aspects of the music such as tempo, pitch, and rhythm.

HOW IS IT BUILT?

The Beat is built in C++ using OpenGL and designed to be optimized on a Nvidia RTX5090 graphics card equipped workstation.
