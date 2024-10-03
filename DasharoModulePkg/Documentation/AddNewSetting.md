This document is meant to outline the process of adding a new Dasharo setting.
If something isn't covered, follow structure of the code used for some similar
variable.  If possible, use alphabetic sorting of lists, although it's not a
requirement.

Examples below use a new setting called `NewSetting`/`NEW_SETTING` which should
be replaced with an actual name of the setting.

Basic variable implementation
=============================

EFI variables are managed by a dedicated library which is used by the UI code.
It makes sense to start from here.

Add variable name in `Include/DasharoOptions.h`
-----------------------------------------------

Add a new `#define DASHARO_VAR_*` near the top where other settings marked with
`// Settings` are, for example:

```
#define DASHARO_VAR_NEW_SETTING           L"NewSetting"
```

This is an EFI variable name.  It should look roughly like the already existing
ones.  The rest of the code should use the macro name after adding
`#include <DasharoOptions.h>` and not the string literal which can have typos.

Add default value in `Library/DasharoVariablesLib/DasharoVariablesLib.c`
------------------------------------------------------------------------

Add another `if`-statement to `GetVariableDefault()` setting default variable
data, its size and additional EFI variable attributes if they are necessary.
See below if variable data is not a primitive type.

Updating `GetVariableDefault()` enables resetting and creation (see the next
step) of the variable with the correct value.

If the variable's data is a structure then additionally:
1. Add a new `DASHARO_*` structure at the bottom of `Include/DasharoOptions.h`.
2. Add the new structure to `VAR_DATA` in `Include/DasharoOptions.h` as well.

Add variable creation in `Library/DasharoVariablesLib/DasharoVariablesLib.c`
----------------------------------------------------------------------------

By adding one more line to `mAllVariables` array there.

Adding the setting to Dasharo System Features
=============================================

Before a variable can become visible in the UI, the code which implements the UI
part needs to become aware of it.

Add storage for new variable
----------------------------

Find `DASHARO_FEATURES_DATA` structure in
`Library/DasharoSystemFeaturesUiLib/DasharoSystemFeaturesHii.h` and add a new
field after `// Feature data` like:

```
  BOOLEAN      NewSetting;
```

Add initialization to constructor of `DasharoSystemFeaturesUiLib`
-----------------------------------------------------------------

Update `DasharoSystemFeaturesUiLibConstructor()` in
`Library/DasharoSystemFeaturesUiLib/DasharoSystemFeatures.c` by adding a new
line like this:

```
  LOAD_VAR (DASHARO_VAR_NEW_SETTING, NewSetting);
```

`NewSetting` here is a field of `DASHARO_FEATURES_DATA` from the above step.

Add saving of new value
-----------------------

`DasharoSystemFeaturesRouteConfig()` in
`Library/DasharoSystemFeaturesUiLib/DasharoSystemFeatures.c` is responsible for
writing changed values back into EFI variable storage.  Add a line like this to
the function:

```
  STORE_VAR (DASHARO_VAR_NEW_SETTING, NewSetting);
```

Very similar to `LOAD_VAR`.

Some variables can even be read-only, but add this line anyway for consistency.
If no value change is detected, nothing gets written.

Exposing the setting in Dasharo System Features UI
==================================================

A variable might exist without being visible to the user, it might be visible
conditionally or unconditionally.  The last two cases require updating UI forms
and tying them with the new code.

Add UI strings to `Library/DasharoSystemFeaturesUiLib/DasharoSystemFeaturesStrings.uni`
---------------------------------------------------------------------------------------

If your variable is called `DASHARO_VAR_NEW_SETTING`, add label and help
prompt like the following:

```
#string STR_NEW_SETTING_PROMPT   #language en-US  "<text that appears in the form>"
#string STR_NEW_SETTING_HELP     #language en-US  "<text that appears in help section of the form>"
```

The two IDs above (`STR_NEW_SETTING_PROMPT` and `STR_NEW_SETTING_HELP`) will be
needed on modifying a form in the next step.

Update `Library/DasharoSystemFeaturesUiLib/DasharoSystemFeaturesVfr.vfr` UI form
--------------------------------------------------------------------------------

The new setting will need to go to one of the sections surrounded by `form` and
`endform`.  In some cases it might be necessary to add a new section.

Most of the settings are booleans that get represented as checkboxes in which
case the added lines would look like this:

```
        checkbox varid   = FeaturesData.NewSetting,
                 prompt  = STRING_TOKEN(STR_NEW_SETTING_PROMPT),
                 help    = STRING_TOKEN(STR_NEW_SETTING_HELP),
                 flags   = RESET_REQUIRED,
        endcheckbox;
```

Making a setting resettable
===========================

Not all UI elements described in VFR file can have their default specified there
as well and that value might not be permanently fixed (i.e., configured at
build-time or run-time).  Handling such a situation requires writing code which
provides default value through a callback which in this case is called
`DasharoSystemFeaturesCallback()` in
`Library/DasharoSystemFeaturesUiLib/DasharoSystemFeatures.c` file.  For the
callback to know which value to provide, it gets an integer value known as
question ID which is specified in VFR file as well.

Create question ID
------------------

Add a new definition at the bottom of
`Library/DasharoSystemFeaturesUiLib/DasharoSystemFeaturesHii.h` like this one:

```
#define NEW_SETTING_QUESTION_ID       0x8010
```

The integer value should be unique.

Extend callback in `Library/DasharoSystemFeaturesUiLib/DasharoSystemFeatures.c`
-------------------------------------------------------------------------------

Handle a new case to `switch (QuestionId)` in `DasharoSystemFeaturesCallback()`
like the following:

```
      case NEW_SETTING_QUESTION_ID:
        Value->b = DasharoGetVariableDefault (DASHARO_VAR_NEW_SETTING).Boolean;
        break;
```

Depending on the variable type you want to change `->b` and `.Boolean` parts.

Update UI element in VFR file
-----------------------------

Find the element in question in
`Library/DasharoSystemFeaturesUiLib/DasharoSystemFeaturesVfr.vfr` and add two
fields to it (update flags if they already exist):

```
                  questionid = NEW_SETTING_QUESTION_ID,
                  flags = RESET_REQUIRED | INTERACTIVE,
```

`RESET_REQUIRED` flag means that resetting settings will affect this particular
one as well.

`INTERACTIVE` flag means that the callback will be invoked for this element.
**Adding `questionid` without setting `INTERACTIVE` flag won't have any useful
effect.**

Topics not covered
==================

Could be added in the future:

* Adding a new submenu.
* Controlling feature visibility via PCDs and hiding them in VFR.
