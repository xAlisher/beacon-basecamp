{
  description = "Beacon UI — universal ui_qml module (C++ QtRO backend + QML view)";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.2.0";
    nix-bundle-lgx.url = "github:logos-co/nix-bundle-lgx";
    # Dependency the backend consumes via modules() — attr name MUST match
    # metadata.json "dependencies". logos_beacon owns the zone_sequencer hop, so
    # this points at the beacon repo root (which builds the logos_beacon module).
    logos_beacon.url = "git+file:///home/alisher/basecamp/modules/beacon-basecamp?ref=main";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
