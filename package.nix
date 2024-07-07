{
  cmake,
  glfw,
  libGL,
  ninja,
  stdenv,
}:
stdenv.mkDerivation {
  name = "mousemeasure";

  src = ./.;

  nativeBuildInputs = [
    cmake
    ninja
  ];

  buildInputs = [
    glfw
    libGL
  ];
}
