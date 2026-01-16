from PIL import Image, ImageDraw

# Load the original PNG (which should have black background)
img = Image.open('GnuLogo.png')

# Convert to RGB if needed
if img.mode != 'RGB':
    img = img.convert('RGB')

# Get image size
width, height = img.size

# Create a new image with wooden frame (5 pixels on each side)
frame_width = 5
new_width = width + 2 * frame_width
new_height = height + 2 * frame_width

# Create new image with wooden color background
# Wood color: brown/tan RGB(139, 90, 43)
wood_color = (139, 90, 43)
framed_img = Image.new('RGB', (new_width, new_height), wood_color)

# Add some texture to the frame for wooden effect
draw = ImageDraw.Draw(framed_img)
# Add horizontal grain lines
for i in range(0, new_height, 2):
    lighter_wood = (149, 100, 53) if i % 4 == 0 else (134, 85, 38)
    draw.line([(0, i), (new_width, i)], fill=lighter_wood)

# Paste the original image in the center
framed_img.paste(img, (frame_width, frame_width))

# Save as BMP
framed_img.save('GnuLogo.bmp', 'BMP')
print(f"Image updated: {framed_img.size}, mode: {framed_img.mode}, wooden frame added")
