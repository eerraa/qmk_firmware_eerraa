# ERA automated-build identity -- deliberately separate from firmware options.
#
# Every keyboards/era target, including the atmega32u4 Brick65 runtime
# exception, includes this file. Keep it free of QMK feature switches and ERA
# firmware selectors: loading it must be incapable of changing a binary.
ifndef ERA_BUILD_IDENTITY_OPTIONS_INCLUDED
ERA_BUILD_IDENTITY_OPTIONS_INCLUDED := yes

# The repository-owned configuration name written into every automated
# artifact and manifest. The common variant rules validate it and apply the
# exact diagnostic combination associated with the name.
ERA_BUILD_VARIANT ?= standard

# Print the options visible to this target, their values and their origins,
# then build as normal.
ERA_SHOW_OPTIONS ?= no

# Internal, firmware-inert handshake used by the repository launcher. When
# enabled the make layer prints the resolved name and immutable five-axis tuple
# in one machine-readable line; the launcher refuses artifacts that disagree.
ERA_BUILD_IDENTITY_REPORT ?= no

endif
