{
  description = "Beacon UI — universal ui_qml module (C++ QtRO backend + QML view)";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.2.0";
    nix-bundle-lgx.url = "github:logos-co/nix-bundle-lgx";
    # Dependency the backend consumes via modules() — attr name MUST match
    # metadata.json "dependencies".
    zone_sequencer.url = "git+file:///home/alisher/basecamp/modules/logos-zone-sequencer-module?ref=feat/modernize-modules-context";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
