{
  description = "beacon-basecamp — on-chain CID inscription module";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
  };

  outputs = { self, logos-module-builder }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      name = "beacon-basecamp";
    };
}
