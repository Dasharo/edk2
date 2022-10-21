#include "FrontPage.h"

VOID
GetDeviceNameFromProduct (
  IN      CHAR16                  *Product,
  OUT     CHAR16                  **DeviceName
  )
{
  // grouped by platform
  //SNB/IVB
  if (!StrCmp(Product, L"Butterfly")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Pavilion Chromebook 14");
  } else if (!StrCmp(Product, L"Link")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Google Chromebook Pixel 2013");
  } else if (!StrCmp(Product, L"Lumpy")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Samsung Chromebook Series 5 550");
  } else if (!StrCmp(Product, L"Parrot")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook C7/C710");
  } else if (!StrCmp(Product, L"Stout")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo ThinkPad X131e Chromebook");
  } else if (!StrCmp(Product, L"Stumpy")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Samsung Chromebox Series 3");
  }
  //HSW
  else if (!StrCmp(Product, L"Falco")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 14");
  } else if (!StrCmp(Product, L"Leon")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Toshiba Chromebook");
  } else if (!StrCmp(Product, L"Mccloud")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebox CXI");
  } else if (!StrCmp(Product, L"Monroe")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"LG Chromebase");
  } else if (!StrCmp(Product, L"Panther")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebox CN60");
  } else if (!StrCmp(Product, L"Peppy")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook C720");
  } else if (!StrCmp(Product, L"Tricky")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Chromebox 3010");
  } else if (!StrCmp(Product, L"Wolf")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Chromebook 11 2013");
  } else if (!StrCmp(Product, L"Zako")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebox CB1");
  }
  //BDW
  else if (!StrCmp(Product, L"Auron")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 11 [C740/C910]");
  } else if (!StrCmp(Product, L"Auron_Paine")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 11 [C740]");
  } else if (!StrCmp(Product, L"Auron_Yuna")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 15 [C910]");
  } else if (!StrCmp(Product, L"Buddy")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebase 24");
  } else if (!StrCmp(Product, L"Gandof")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Toshiba Chromebook2 2015");
  } else if (!StrCmp(Product, L"Guado")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebox 2 / CN62");
  } else if (!StrCmp(Product, L"Lulu")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Chromebook 13 7310");
  } else if (!StrCmp(Product, L"Rikku")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebox CXI2");
  } else if (!StrCmp(Product, L"Samus")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Google Chromebook Pixel 2015");
  } else if (!StrCmp(Product, L"Tidus")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo ThinkCentre Chromebox");
  }
  //BYT
  else if (!StrCmp(Product, L"Banjo")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 15 [CB3-571]");
  } else if (!StrCmp(Product, L"Candy")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Chromebook 11 3120");
  } else if (!StrCmp(Product, L"Clapper")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo N20 Chromebook");
  } else if (!StrCmp(Product, L"Enguarde")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo N21 Chromebook");
  } else if (!StrCmp(Product, L"Glimmer")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo ThinkPad 11e");
  } else if (!StrCmp(Product, L"Gnawty")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 11 [CB3-111/131]");
  } else if (!StrCmp(Product, L"Heli")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Haier Chromebook G2");
  } else if (!StrCmp(Product, L"Kip")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 11 G3/G4");
  } else if (!StrCmp(Product, L"Ninja")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"AOpen Chromebox Commercial");
  } else if (!StrCmp(Product, L"Orco")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo Ideapad 100s");
  } else if (!StrCmp(Product, L"Quawks")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook C300");
  } else if (!StrCmp(Product, L"Squawks")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook C200");
  } else if (!StrCmp(Product, L"Sumo")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"AOpen Chromebase Commercial");
  } else if (!StrCmp(Product, L"Swanky")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Toshiba Chromebook2 2014");
  } else if (!StrCmp(Product, L"Winky")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Samsung Chromebook 2");
  }
  //BSW
  else if (!StrCmp(Product, L"Banon")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 15 [CB5-532]");
  } else if (!StrCmp(Product, L"Celes")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Samsung Chromebook 3");
  } else if (!StrCmp(Product, L"Cyan")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook R11 [C738T]");
  } else if (!StrCmp(Product, L"Edgar")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 14");
  } else if (!StrCmp(Product, L"Kefka")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Chromebook 11 3180/3189");
  } else if (!StrCmp(Product, L"Reks")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo N22 Chromebook");
  } else if (!StrCmp(Product, L"Relm")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 11 N7");
  } else if (!StrCmp(Product, L"Setzer")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 11 G5");
  } else if (!StrCmp(Product, L"Terra")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook C202SA/C300SA");
  } else if (!StrCmp(Product, L"Ultima")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo ThinkPad 11e/Yoga Chromebook (G3)");
  } else if (!StrCmp(Product, L"Wizpig")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"White Box Braswell Chromebook");
  }
  //SKL
  else if (!StrCmp(Product, L"Asuka")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Chromebook 13 3380");
  } else if (!StrCmp(Product, L"Caroline")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Samsung Chromebook Pro");
  } else if (!StrCmp(Product, L"Cave")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook Flip C302");
  } else if (!StrCmp(Product, L"Chell")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 13 G1");
  } else if (!StrCmp(Product, L"Lars")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 14 for Work");
  } else if (!StrCmp(Product, L"Sentry")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo ThinkPad 13 Chromebook");
  }
  //APL
  else if (!StrCmp(Product, L"Astronaut")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 11 (C732)");
  } else if (!StrCmp(Product, L"Babymako")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook C403");
  } else if (!StrCmp(Product, L"Babymega")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook C223NA");
  } else if (!StrCmp(Product, L"Babytiger")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook C523NA");
  } else if (!StrCmp(Product, L"Blacktip")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"CTL Chromebook NL7/NL7T");
  } else if (!StrCmp(Product, L"Blue")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 15 [CB315]");
  } else if (!StrCmp(Product, L"Bruce")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook Spin 15 [CP315]");
  } else if (!StrCmp(Product, L"Electro")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook Spin 11 [R751T]");
  } else if (!StrCmp(Product, L"Epaulette")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 514");
  } else if (!StrCmp(Product, L"Lava")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook Spin 11 [CP311]");
  } else if (!StrCmp(Product, L"Nasher")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Chromebook 11 5190");
  } else if (!StrCmp(Product, L"Nasher360")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Chromebook 11 5190 2-in-1");
  } else if (!StrCmp(Product, L"Pyro")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo Thinkpad 11e/Yoga 11e [G4]");
  } else if (!StrCmp(Product, L"Rabbid")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook C423");
  } else if (!StrCmp(Product, L"Reef")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook Flip C213SA");
  } else if (!StrCmp(Product, L"Robo")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo 100e Chromebook");
  } else if (!StrCmp(Product, L"Robo360")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo 500e Chromebook");
  } else if (!StrCmp(Product, L"Sand")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 15 [CB515-1HT]");
  } else if (!StrCmp(Product, L"Santa")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 11 [CB311-8H]");
  } else if (!StrCmp(Product, L"Snappy")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook x360 11 G1/11 G6/14 G5");
  } else if (!StrCmp(Product, L"Whitetip")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"CTL Chromebook J41/J41T");
  }
  //KBL
  else if (!StrCmp(Product, L"Akali")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 13");
  } else if (!StrCmp(Product, L"Akali 360")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook Spin 13");
  } else if (!StrCmp(Product, L"Atlas")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Google Pixelbook Go 2019");
  } else if (!StrCmp(Product, L"Bard")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 715");
  } else if (!StrCmp(Product, L"Ekko")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 714");
  } else if (!StrCmp(Product, L"Eve")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Google Pixelbook 2017");
  } else if (!StrCmp(Product, L"Jax")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"AOpen Chromebox Commercial 2");
  } else if (!StrCmp(Product, L"Karma")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebase 24I2");
  } else if (!StrCmp(Product, L"Kench")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebox G2");
  } else if (!StrCmp(Product, L"Leona")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook C425");
  }else if (!StrCmp(Product, L"Nautilus")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Samsung Chromebook Plus V2");
  } else if (!StrCmp(Product, L"Nocturne")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Google Pixel Slate");
  } else if (!StrCmp(Product, L"Pantheon")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo Yoga Chromebook C630");
  } else if (!StrCmp(Product, L"Rammus")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook C425/C433/C434");
  } else if (!StrCmp(Product, L"Shyvana")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook C433/C434");
  }else if (!StrCmp(Product, L"Sion")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebox CXI3");
  } else if (!StrCmp(Product, L"Sona")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook x360 14");
  } else if (!StrCmp(Product, L"Soraka")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook x2");
  } else if (!StrCmp(Product, L"Syndra")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 15 G1");
  } else if (!StrCmp(Product, L"Teemo")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebox 3 / CN65");
  } else if (!StrCmp(Product, L"Vayne")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Inspiron Chromebook 14");
  } else if (!StrCmp(Product, L"Wukong")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"CTL Chromebox CBx1");
  }
  //GLK
  else if (!StrCmp(Product, L"Ampton")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook Flip C214/C234");
  } else if (!StrCmp(Product, L"Apel")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook Flip C204");
  } else if (!StrCmp(Product, L"Bloog")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook x360 12b");
  } else if (!StrCmp(Product, L"Blooglet")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 14a");
  } else if (!StrCmp(Product, L"Blooguard")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook x360 14a/14b");
  } else if (!StrCmp(Product, L"Blorb")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 315");
  } else if (!StrCmp(Product, L"Bluebird")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Samsung Chromebook 4");
  } else if (!StrCmp(Product, L"Bobba")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 311");
  } else if (!StrCmp(Product, L"Bobba360")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook Spin 311/511");
  } else if (!StrCmp(Product, L"Casta")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Samsung Chromebook 4+");
  } else if (!StrCmp(Product, L"Dood")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"NEC Chromebook Y2");
  } else if (!StrCmp(Product, L"Dorp")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 14 G6");
  } else if (!StrCmp(Product, L"Droid")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 314");
  } else if (!StrCmp(Product, L"Fleex")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Chromebook 3100");
  } else if (!StrCmp(Product, L"Garg")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"CTL Chromebook NL71/CT/LTE");
  } else if (!StrCmp(Product, L"Garg360")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"CTL Chromebook NL71T/TW/TWB");
  } else if (!StrCmp(Product, L"Garfour")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"CTL Chromebook NL81/NL81T");
  } else if (!StrCmp(Product, L"Glk")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 311");
  } else if (!StrCmp(Product, L"Glk360")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook Spin 311");
  } else if (!StrCmp(Product, L"Grabbiter")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Chromebook 3100 2-in-1");
  } else if (!StrCmp(Product, L"Foob")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"CTL Chromebook VX11/VX11T");
  } else if (!StrCmp(Product, L"Foob360")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Poin2 Chromebook 11P");
  } else if (!StrCmp(Product, L"Laser")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo Chromebook C340");
  } else if (!StrCmp(Product, L"Laser14")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo Chromebook S340");
  } else if (!StrCmp(Product, L"Lick")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo Ideapad 3 Chromebook");
  } else if (!StrCmp(Product, L"Meep")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook x360 11 G2 EE");
  } else if (!StrCmp(Product, L"Mimrock")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 11 G7 EE");
  } else if (!StrCmp(Product, L"Nospike")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook C424");
  } else if (!StrCmp(Product, L"Orbatrix")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Chromebook 3400");
  } else if (!StrCmp(Product, L"Phaser")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo 100e Chromebook 2nd Gen");
  } else if (!StrCmp(Product, L"Phaser360")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo 300e Chromebook 2nd Gen");
  } else if (!StrCmp(Product, L"Phaser360s")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo 500e Chromebook 2nd Gen");
  } else if (!StrCmp(Product, L"Sparky")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 512");
  } else if (!StrCmp(Product, L"Sparky360")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook Spin 512");
  } else if (!StrCmp(Product, L"Vorticon")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 11 G8 EE");
  } else if (!StrCmp(Product, L"Vortininja")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook x360 11 G3 EE");
  }
  //STR
  else if (!StrCmp(Product, L"Aleena")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 315");
  } else if (!StrCmp(Product, L"Barla")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 11A G6 EE");
  } else if (!StrCmp(Product, L"Careena")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 14");
  } else if (!StrCmp(Product, L"Grunt")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"AMD StonyRidge Chromebook");
  } else if (!StrCmp(Product, L"Kasumi")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 311");
  } else if (!StrCmp(Product, L"Liara")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo 14e Chromebook");
  } else if (!StrCmp(Product, L"Treeya")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo 100e Chromebook 2nd Gen");
  } else if (!StrCmp(Product, L"Treeya360")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo 300e Chromebook 2nd Gen");
  }
  //WHL
  else if (!StrCmp(Product, L"Arcada")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Latitude 5300 2-in-1 Chromebook Enterprise");
  } else if (!StrCmp(Product, L"Sarien")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Latitude 5400 Chromebook Enterprise");
  }
  //CML
  else if (!StrCmp(Product, L"Akemi")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo Ideapad Flex 5 Chromebook");
  } else if (!StrCmp(Product, L"Dragonair")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook x360 14c");
  } else if (!StrCmp(Product, L"Drallion")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Dell Latitude 7410 Chromebook Enterprise");
  } else if (!StrCmp(Product, L"Dratini")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Pro c640 Chromebook");
  } else if (!StrCmp(Product, L"Duffy")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"ASUS Chromebox 4");
  } else if (!StrCmp(Product, L"Faffy")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"ASUS Fanless Chromebox");
  } else if (!StrCmp(Product, L"Helios")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Asus Chromebook Flip C436FA");
  } else if (!StrCmp(Product, L"Jinlon")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Elite c1030 Chromebook / Chromebook x360 13c");
  } else if (!StrCmp(Product, L"Kaisa")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebox CXI4");
  } else if (!StrCmp(Product, L"Kindred")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 712");
  } else if (!StrCmp(Product, L"Kled")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook Spin 713 [CP713-2W]");
  } else if (!StrCmp(Product, L"Kohaku")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Samsung Galaxy Chromebook");
  } else if (!StrCmp(Product, L"Nightfury")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Samsung Galaxy Chromebook 2");
  } else if (!StrCmp(Product, L"Noibat")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebox G3");
  } else if (!StrCmp(Product, L"Wyvern")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"CTL Chromebox CBx2");
  } 
  //Picasso/Dali
    else if (!StrCmp(Product, L"Morphius")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Lenovo ThinkPad C13 Yoga Chromebook");
  }
  //Jasperlake
    else if (!StrCmp(Product, L"Drawlat")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 11 G9 EE");
  } else if (!StrCmp(Product, L"Drawman")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook 14 G7");
  } else if (!StrCmp(Product, L"Drawcia")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Chromebook x360 11 G4 EE");
  }
  //Tigerlake
    else if (!StrCmp(Product, L"Delbin")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"ASUS Chromebook Flip CX5");
  } else if (!StrCmp(Product, L"Drobit")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"ASUS Chromebook CX9400");
  } else if (!StrCmp(Product, L"Elemi")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"HP Pro c640 G2 Chromebook");
  } else if (!StrCmp(Product, L"Lillipup")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"IdeaPad Flex 5i Chromebook");
  } else if (!StrCmp(Product, L"Volta")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook 514 [CB514-1H]");
  } else if (!StrCmp(Product, L"Voxel")) {
    StrCatS (*DeviceName, 0x60 / sizeof (CHAR16), L"Acer Chromebook Spin 713 [CP713-3W]");
  }
}
