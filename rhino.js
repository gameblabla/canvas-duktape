const canvas = document.getElementById("canvas");
const ctx = canvas.getContext("2d");
var image = new Image();

image.addEventListener("load", function() {
  ctx.drawImage(image, 33, 71, 104, 124, 21, 20, 87, 104);
});

image.src = 'rhino.jpg';