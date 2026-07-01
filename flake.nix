{
  description = "beacon-basecamp — on-chain CID inscription module";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.2.0";
    nixpkgs.follows = "logos-module-builder/nixpkgs";
    # zone_sequencer — consumed via modules() (attr name MUST match metadata deps).
    zone_sequencer.url = "git+file:///home/alisher/basecamp/modules/logos-zone-sequencer-module?ref=feat/modernize-modules-context";
  };

  outputs = inputs@{ self, logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
