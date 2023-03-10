// #include <iostream>
// using namespace std;
 
 


/* Грамматика для логического выражения
S -> E&
E -> T | T '|' E | T '&' E
T -> F | F '>' T | F '<' T | F '=' T
F -> 0-9 | '(' E ')' | '!' F
*/



class Expression {

  public:
    String expression;    
    String currentLex; // Текущая лексема
    int pos = -1; 

    Expression () {
      expression.reserve(50);
      currentLex.reserve(10);
    }

    float evaluate (String &expression_) {
      pos = -1;
      expression = expression_;      

      getNextLexeme();
      try {                    
          return parseS();          
      } catch ( const char * err ) {
          Serial.print( "Error: "); Serial.println( err ) ; // << ", but " << currentLex << " got." << endl;
      }
    }
    
    inline void getNextLexeme() // Функция получения следующей лексемы 
    {   pos++;
        while(expression[pos] == ' ') pos++;

        
        if (pos < expression.length()) {        
            currentLex = expression[pos];
            if(expression[pos] >= '0' && expression[pos] <='9') {          
              while((expression[pos+1] >= '0' && expression[pos+1] <='9') || expression[pos+1] == '.') {
                currentLex += expression[++pos];
              }
            }        
        } else {
            currentLex = "";
        }
        // Serial.print( " [lex: " ); Serial.print( currentLex.c_str() ); Serial.println("] ");
    }


    
    float parseS()
    {
        float r = parseE();
        if ( pos != expression.length() ) { // Проверяем конец цепочки
            throw "End of line needed";
        }
        return r;
    }
    
    float parseE()
    {
        float b = parseT();
    
        if (currentLex == "|")
        {
            getNextLexeme();
            float b2 = parseE();         
            Serial.print( b ); Serial.print( '|' );
            b = b2 || b;
            Serial.print( b2 ); Serial.print( "  => " ); Serial.println(b);
        } else if (currentLex == "&") {
            getNextLexeme();
            float b2 = parseE();
            Serial.print( b ); Serial.print( '&' );
            b = b && b2;
            Serial.print( b2 ); Serial.print( "  => " ); Serial.println(b);
        }
        

        return b;
    }
    
    float parseT()
    {
        float b = parseF();

        if (currentLex == ">") {
            getNextLexeme();
            float b2 = parseT();
            Serial.print( b ); Serial.print( '>' );
            b = b > b2;
            Serial.print( b2 ); Serial.print( "  => " ); Serial.println(b);

        } else if (currentLex == "<") {
            getNextLexeme();
            float b2 = parseT();
            Serial.print( b ); Serial.print( '<' );
            b = b < b2;
            Serial.print( b2 ); Serial.print( "  => " ); Serial.println(b);
          
        } else if (currentLex == "=") {        
            getNextLexeme();
            float b2 = parseT();
            Serial.print( b ); Serial.print( '=' );
            b = b2 == b;
            Serial.print( b2 ); Serial.print( "  => " ); Serial.println(b);
        }
        
        return b;
    }
    
    bool isNumeric(String &s)  {
        return s == "0" || s.toFloat() != 0;
    }
    
    float parseF()
    {   // Serial.print( "ParseF: " << currentLex;
        float b;
        if ( isNumeric(currentLex) ) 
        {
            b = currentLex.toFloat(); //(currentLex != '0')
            getNextLexeme();
        }
        else if ( currentLex == "!" ) 
        {
            getNextLexeme();
            b = !parseF();
        }
        else if ( currentLex == "(" ) 
        {
            getNextLexeme();
            b = parseE(); 
            if ( currentLex != ")" ) {
                throw ") needed";
            }
            getNextLexeme();
        }
        else {
            throw " 0 or 1 or ! or ( needed";
        }

        // Serial.print( ", res: " << b <<'\n';
        return b;
    }
};

