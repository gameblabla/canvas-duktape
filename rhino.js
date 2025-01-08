const canvas = document.getElementById("canvas");
const ctx = canvas.getContext("2d");
const image = document.images[0];

image.addEventListener("load", function() {
  ctx.drawImage(image, 33, 71, 104, 124, 21, 20, 87, 104);
});

