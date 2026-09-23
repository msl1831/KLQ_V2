import klq

klq.display_face(1)
klq.wait(0.5)
for _ in range(2):
    klq.display_number(7)
    klq.wait(0.5)
    klq.display_face(5)
    klq.wait(0.5)
klq.display_off()
