#include <studio.h>
#include "increment.h"

// Increment işlemini gerçekleştiren fonksiyon
int increment(int number) {
    return number + 1;
}

// Girişten sayı okuyan ve sonucu yazdıran ana fonksiyon
int main() {
    int number;
    if (scanf("%d", &number) == 1) { // Standart girişten bir sayı oku
        printf("%d\n", increment(number)); // Sayıyı bir artır ve çıktıya yazdır
        return 0; // Başarıyla çık
    }
    fprintf(stderr, "Invalid input\n"); // Eğer sayı okunamazsa hata mesajı yaz
    return 1; // Hata kodu ile çık
}