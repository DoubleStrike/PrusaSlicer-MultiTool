
![PrusaSlicer logo](/resources/icons/PrusaSlicer.png?raw=true)

# PrusaSlicer-MultiTool

**This is a fork of PrusaSlicer that removes various warnings related to multi-toolhead operation, such as blocking wipe towers with mismatching nozzle sizes, even if the layer height is a sane number.**  *This will likely cause tons of issues unless you are watching closely and have a specific use case in mind.  Caveat emptor!*

Please note that when using dissimilar nozzle sizes, as per BaconFase on the Prusa forums, you will want to set your min and max layer heights **for all 5 extruders** to zero.  We are not 100% sure of what else this may affect, but we are doing additional testing to determine specifics. If code changes can help, I'll look into those.
![e715e71f-untitled](https://github.com/user-attachments/assets/03b94596-dec7-42d9-adff-3e50207c7199)

---

You may want to check the upstream [PrusaSlicer project page](https://github.com/prusa3d/PrusaSlicer) for more info and in case I stop maintaining this build in the future.

---

### Final warning: this code is likely to destroy your print, melt your printer, set your house on fire, and make your wife or husband leave you. The world's end may be hastened, and cats and dogs will live together in an abomination of the natural order.  You have been warned.
