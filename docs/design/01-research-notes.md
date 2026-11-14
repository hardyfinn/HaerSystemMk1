# Autechre's post-2008 tools: what is publicly known

Research notes for the Autechre-style VCV Rack module project. Each point is tagged:

- **[Band]** Sean Booth or Rob Brown's own words (quoted from the source, or close paraphrase).
- **[Band, 2nd-hand]** a band quote relayed by Wikipedia or another article; original not checked.
- **[Press]** a journalist's or editor's description.
- **[Inferred]** my reading of the music or the sources. Not documented.

Main sources read: the 2013 WATMM "Ask Autechre Anything" thread (via a fan compilation), Pitchfork's 2018 NTS Sessions interview (Philip Sherburne, mirrored on aepages.org), Sasha Frere-Jones's 2020 Sign/Plus interview, MusicRadar's 2024 "Pioneers" feature (relaying Resident Advisor 2016), a 2016 Radio Študent interview (via Wikipedia), and the Wikipedia pages for Exai, elseq 1-5, NTS Sessions 1-4, SIGN, PLUS and AE_LIVE. The full 2013 WATMM thread, the aepages interview archive and Sean's Mastodon posts could not be fetched from here.

## 1. The system as a whole

- **[Band]** "we use max as a hub really, with c externals and gen, few bits of hardware" (2013). Hardware use is "increasingly less".
- **[Band]** "part of the reason we make software is so that we can hack it easily, and save tons of versions" (2013).
- **[Band, 2nd-hand]** "I just use Max/MSP now, because in Max I can generally build the thing I need" (RA, 2016). elseq 1-5 is described as made entirely in Max with no external instruments (Wikipedia, uncited).
- **[Band]** Since about 2006 they moved from one algorithm per track to "a more general system that we can expand as we go" (2013). In 2018 Booth said "the system" is "about eight years old", i.e. the Exai-era rig (c. 2010).
- **[Band, 2nd-hand]** Exai: they dropped MIDI after Quaristice because it "wasn't quite what [they] wanted to be doing". Many Exai tracks were trial runs of the system they were building for live shows.
- **[Band]** "The MIDI keyboard only handles a very limited set of information... The piano is beautiful but it's dumb." (2020)
- **[Band, 2nd-hand]** The same system was used for the 2014-15 live set and for elseq 1-5 (2016).
- **[Band]** They work apart and swap patches: "two guys sharing a computer doesn't really work. But two guys swapping patches works really well." (2018). For SIGN/PLUS, Booth wrote pieces of software, sent them to Brown, and got "weird tracks" back (2020).
- **[Band]** "Our system is great for making Autechre tracks, but I'm not sure if everybody else wants to do that." "We have a user base of two." (2018)

## 2. Sequencing and composition

- **[Band]** "the line between sequencing and synthesis is pretty much gone now", "textures are sequences, sequences are like harmonies" (2013).
- **[Band]** On their sequencers: "we developed them a fair amount since, they're not as trad as they were then" (2013).
- **[Band]** "you can have a patch that's essentially a sequencer but it only makes one sequence". "If you make a sequencer that only makes one type of sequence, and you've used it twice, then I guess you've used the same musical idea twice." Brown: "It's like a hard-wired piece of notation in a way." (2018)
- **[Band, 2nd-hand]** "a lot of the sequencing is still very complex, but it's a lot of curves rather than discrete events" (2016, about the 2016/2018 live set).
- **[Band]** Oversteps (2010): "most of it's markov chain stuff done in max, with conditionals forcing scales and harmonies". "the voices were independent but communicating it's just conditionals really nothing too fancy". (2013)
- **[Band]** "sometimes it's all programmed step by step, sometimes played in", and things are "defined in advance and then run off in realtime" (2013).
- **[Band]** "everything is continuous basically, there is no recording as such, just a stream of data being generated strictly" (2013).
- **[Band]** "The timeline's still alive" / "Nothing's frozen... there's quite a lot of room for fuckery" (2018): patches stay editable rather than being rendered into fixed parts.
- **[Band]** Not everything is in Max: a 'T ess xi' sequence and the polyrhythmic 'UnderBOAC' were done in Logic.

## 3. Generative behaviour and randomness

- **[Band]** They prefer "procedural" to "generative": things are planned and "codified", then executed (2013).
- **[Band]** "it's easy enough not to use random in max... not that i'm against it any more... as long as it's done carefully noise can be really useful" (2013).
- **[Band]** "describing a series of if statements as ai is a bit reaching" (2013).
- **[Band, 2nd-hand]** The process is "not entirely" generative; a human has to guide it, and "as little direction as possible" works best (NYT, 2020).
- **[Band]** Booth likes fluid simulation (Navier-Stokes) as a way to move parameters "in a controllable but beautiful way", easier since gen (2013). Brown: "i prefer moire effects in music more than fractal" (2013).
- **[Inferred]** "Moiré" and "curves rather than discrete events" point to interacting periodic processes at close-but-unequal rates (phase drift, beating LFOs) rather than dice rolls.

## 4. Rhythm and time

- **[Band]** "the rhythm stuff has been second nature for a while, in terms of programming and algos" (2013).
- **[Band]** Booth: David Lynch is an influence "because of the way he uses time" (2018).
- **[Press]** SIGN tracks often sit on an 80-110 BPM hip-hop-like gait under abstract textures.
- **[Inferred, from listening]** The signature moves on Exai, elseq and NTS: tempo that bends continuously inside a bar; drum hits whose timing is pulled by a curve rather than a swing grid; patterns that change length and density gradually; rolls and ratchets with accelerating or decelerating spacing; hits that fragment into granular smears. None of this has a published spec.

## 5. Synthesis and sound

- **[Band]** Software synths and "virtual machines" built in Max/gen with C externals; Navier-Stokes simulation used for sound and control (2013, 2018).
- **[Band]** Rob built a video/sample cut-up patch c. 2005-06 that was reused on Exai ('deco Loc'); percussion samples of "wood and rubber" (2013, via aepages).
- **[Band]** Rob: "glottal shit on the bas[s]line" on 'tuinorizn' (2013, via aepages).
- **[Press / fans]** Widely heard as heavy FM and physical modelling. No band statement in these sources confirms FM by name.
- **[Inferred]** Struck/resonant percussion (modal or waveguide), FM bass and bells with modulated index and ratio, formant/vocal-tract filtering ("glottal"), and stuttered sample-cut textures cover most of the palette.

## 6. Effects and mix

- **[Band]** Booth is "really interested in deep mixing now": "a sound stage that's got almost like a 3D feel" (2018), after a 2014 live set that was "almost a cartoon-like version of club music".
- **[Press / fans]** A distinctive long reverb tail recurs across Exai's 'bladelores' and NTS tracks (fan claim, uncited).
- **[Inferred]** Custom reverbs with modulated, sometimes unstable feedback; spatial placement as a compositional parameter.

## 7. Live performance

- **[Band, 2nd-hand]** Exai came from "long jams edited down"; "some of it was worked on more compositionally (the software can do both)" (2013).
- **[Band]** NTS Sessions: "we had enough material in the patches, we could fill eight hours"; the oldest jam is from 2011, "The rest is weird recent jams using old patches"; "We spent ages sequencing it... re-editing bits" (2018).
- **[Band, 2nd-hand]** The 2016/2018 live set was "a little bit slower, and a little bit more focused on the sound than the beat".
- **[Press]** Shows are played in total darkness, deliberately removing the performers from view. The AE_LIVE releases are full sets rendered from the system.
- **[Inferred]** Live control is macro-level: steering a few high-level parameters of many interacting processes, not playing notes.

## 8. What this means for a VCV design

1. Make sequencers that embody one idea each and can be swapped and hacked, rather than one giant generic sequencer.
2. Favour continuous curves and interacting rates over random triggers; offer randomness carefully and seedably.
3. Blur rhythm and synthesis: the same modules should run at audio rate as well as event rate.
4. Use constraint rules (scales, harmonic conditionals) on top of Markov-like note choice.
5. Give the performer macro controls that move many parameters along curves, and make state savable and recallable.
6. Treat space (reverb, depth) as a compositional voice.
