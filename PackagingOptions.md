# Packaging Options

TallyTrek is usually packaged in the 3D printed NiftyCase, but a counter can go in anything that keeps it dry. The NiftyCase was designed by Loren
Konkus for NiftyCounter, and the other options below come from his original trail-counters project.

- NiftyCase Box
- ZipLoc Bag
- ABS Waterproof Box

## NiftyCase Box

The NiftyCase box holds a Feather stack, a sensor, and a battery in a case that can be screwed to a 4x4 post or a tree.

![case](/assets/images/NiftyCaseClosed.jpg)
![case](/assets/images/NiftyCaseTree.jpg)

Here it is open. This one holds the STHS34PF80 infrared sensor from an earlier NiftyCounter variant, mounted on the inside of the lid. TallyTrek's
RCWL-0516 microwave sensor goes in the same spot.

![case](/assets/images/NiftyCaseOpen.jpg)

There's plenty of space reserved for larger batteries.

The STL files are NiftyCaseBox.stl, NiftyCaseLid.stl, NiftyCaseLatch.stl (two required) and NiftyCaseTPUSeal.stl. Since these are exposed to the
elements, print them in a material such as PETG or ABS for longevity. The NiftyCaseTPUSeal is an optional flexible gasket that helps make the box
more weathertight, and should be printed in something like TPU.

### Mounting the RCWL-0516

- The side of the sensor with the components on it is the front. Mount it flat against the inside of the lid with the component side facing out,
  toward the trail.
- Mount it at the end of the lid with the hole. When the case is closed, that end sits over the empty part of the box, away from the battery.
- Keep the back of the sensor at least 1 cm from metal, including the Feather stack and the battery. Don't put metal screws, metallic tape, or labels
  in front of the sensor. Nylon screws or double-sided foam tape are safest. The bosses by the hole were spaced for the infrared sensor's board, so
  the RCWL-0516's mounting holes may not line up.
- The hole in the lid was made for the infrared sensor, which can't see through plastic. Microwaves pass through the lid, so the RCWL-0516 doesn't
  need it. Lining Q1 (the transistor in the middle of the antenna) up with the hole is a fine way to position the sensor, but seal the hole from the
  outside with tape or a flush plug so rain, condensation, and bugs can't reach the sensor. Leave a small air gap over Q1 rather than filling the hole
  with silicone.
- Leave the RCWL-0516's CDS pads empty. A light sensor there would make it detect only in the dark.

## ZipLoc Bag

The simplest option when deploying a trail counter is to tuck it in a ZipLoc bag. These are waterproof, low cost, and easy to fit into small, odd
spaces. Bags are great for quickly deploying a prototype out on the trail.

![ziploc](/assets/images/ziploc.jpg)

On the downside, they aren't particularly robust, are vulnerable to animals, and may be mistaken for rubbish. Some sensors are very sensitive to
their surroundings and may be affected by where the batteries and boards end up, and that's hard to control in a bag.

## ABS Waterproof Box

Commercial ABS waterproof junction boxes are solid and robust and available in many sizes and formats. Here's one that's been used for a counter.

![case](/assets/images/exposurecase.jpg)

This one is 6"x4"x3" and is available at https://www.amazon.com/Zulkit-Waterproof-Electrical-Transparent-150x100x70/dp/B07RPNWD47

Being ABS, it's easy to use styrene solvent to glue plastic standoffs wherever needed to mount a circuit board.

![case](/assets/images/exposurecasesensor.jpg)
![case](/assets/images/exposurecaseclosed.jpg)

Microwave sensors like the RCWL-0516 can sense through wood, so a good placement is on a support beam under a bridge. Narrow bridges encourage users
to pass single file and at relatively slow speeds, which helps keep counts accurate.

![case](/assets/images/exposurecasebridge.jpg)

## Tagging

Whatever package you use, it's good practice to include your name (or your organization's name) and a contact phone number in the package. If the
package is misplaced, this information may help it find its way home. If it's found by a suspicious individual, a phone call can help the bomb squad
determine that it's a LiPo battery and not C4.
